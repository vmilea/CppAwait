/*
* Copyright 2012 Valentin Milea
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*   http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include <Looper/Looper.h>
#include <CppAwait/impl/Util.h>

using namespace ut;

namespace loo {

static Looper* sMainLooper;

Looper& mainLooper()
{
    return *sMainLooper;
}

void setMainLooper(Looper& mainLooper)
{
    sMainLooper = &mainLooper;
}

Looper::Looper(const std::string& name)
    : mName(name)
    , mThreadId(std::numeric_limits<uint32_t>::max())
    , mQuit(false)
{
    struct SchedulerAdapter : public AbstractScheduler
    {
    public:
        SchedulerAdapter(Looper& looper)
            : looper(looper) { }

        void schedule(const Runnable& action)
        {
            looper.schedule(action);
        }

        void schedule(Runnable&& action)
        {
            looper.schedule(std::move(action));
        }

        Looper& looper;
    };
    
    mSchedulerAdapter.reset(new SchedulerAdapter(*this));
}

Looper::~Looper()
{
}

void Looper::run()
{
    mThreadId = this_thread::id();

    mQuit = false;
    do {
        { ScopedLock<FastMutex> lock(mMutex);
            do {
                Timepoint sleepUntil = mContext.queuePending();

                int32_t timeout = (sleepUntil - getMonotonicTime()).milliseconds();
                if (timeout <= 0)
                    break;

                if (timeout < 2) { // busy wait if less than 2ms until trigger
                    do {
                        { loo::ScopedUnlock<FastMutex> _(mMutex);
                            this_thread::yield();
                        }
                    } while (getMonotonicTime() < sleepUntil && !mContext.hasPending());
                } else {
                    lock.tryWait(timeout);
                }
            } while (true);
        }

        ut_assert_(!mQuit);
        
        mContext.runQueued(&mQuit);

        this_thread::yield();
    } while (!mQuit);

    { loo::ScopedLock<FastMutex> _(mMutex);
        mContext.queuePending(); // delete cancelled actions
    }
}

void Looper::quit()
{
    ut_assert_msg_(this_thread::id() == mThreadId, "%s - quit() called from outside the loop!", mName.c_str());

    cancelAll();
    mQuit = true;
}

bool Looper::cancel(Ticket ticket)
{
    ut_assert_msg_(this_thread::id() == mThreadId, "%s - tryCancel() called from outside the loop!", mName.c_str());

    bool didCancel = mContext.tryCancelQueued(ticket);

    if (!didCancel) {
        { loo::ScopedLock<FastMutex> _(mMutex);
            didCancel = mContext.tryCancelPending(ticket);
        }
    }

    return didCancel;
}

void Looper::cancelAll()
{
    ut_assert_msg_(this_thread::id() == mThreadId, "%s - cancelAll() called from outside the loop!", mName.c_str());

    mContext.cancelAllQueued();

    { loo::ScopedLock<FastMutex> _(mMutex);
        mContext.cancelAllPending();
    }
}

//
// detail
//

namespace detail
{
    struct _LoopContext::ManagedAction
    {
        ManagedAction(Ticket ticket, RepeatingAction&& action, long interval, bool catchUp)
            : ticket(ticket), action(std::move(action)), interval(interval), catchUp(catchUp), triggerTime(0), isCancelled(false) { }

        Ticket ticket;
        RepeatingAction action;
        long interval;
        bool catchUp;
        Timepoint triggerTime;
        bool isCancelled;
    };

    _LoopContext::_LoopContext()
        : mTicketCounter(100)
    {
    }

    _LoopContext::~_LoopContext()
    {
    }

    void _LoopContext::runQueued(bool *quit)
    {
        Timepoint now = getMonotonicTime();

        ut_foreach_(ManagedAction *action, mQueuedActions) {
            if (action->isCancelled)
                continue;

            if (action->triggerTime <= now) {
                bool repeat = false;
                try {
                    repeat = action->action();
                } catch(const std::exception& ex) {
                    ut_log_warn_("Uncaught exception while running loop action: %s", ex.what());
                    throw;
                }

                if (repeat) {
                    if (action->catchUp) {
                        action->triggerTime.addMilli(action->interval);
                    } else {
                        action->triggerTime = now.plusMilli(action->interval);
                    }
                } else {
                    action->isCancelled = true;
                }

                if (*quit) { // running the action may have triggered quit
                    break;
                }
            }
        }
    }

    Timepoint _LoopContext::queuePending() // must have lock
    {
        ut_foreach_(ManagedAction *action, mQueuedActions) {
            if (action->isCancelled) {
                delete action;
            } else {
                mPendingActions.push_back(action);
            }
        }

        mQueuedActions.clear();
        mQueuedActions.swap(mPendingActions);

        Timepoint wakeTime = TIMEPOINT_MAX;

        ut_foreach_(ManagedAction *action, mQueuedActions) {
            if (action->triggerTime < wakeTime) {
                wakeTime = action->triggerTime;
            }
        }

        return wakeTime;
    }

    bool _LoopContext::hasPending() // must have lock
    {
        return !mPendingActions.empty();
    }

    bool _LoopContext::tryCancelQueued(Ticket ticket)
    {
        ut_foreach_(ManagedAction *action, mQueuedActions) {
            if (action->ticket == ticket) {
                if (action->isCancelled) {
                    return false;
                } else {
                    action->isCancelled = true;
                    return true;
                }
            }
        }

        return false;
    }

    bool _LoopContext::tryCancelPending(Ticket ticket) // must have lock
    {
        for (std::vector<ManagedAction *>::iterator it = mPendingActions.begin(), end = mPendingActions.end(); it != end; ++it) {
            ManagedAction *action = *it;

            if (action->ticket == ticket) {
                ut_assert_(!action->isCancelled);

                delete action;
                mPendingActions.erase(it);
                return true;
            }
        }

        return false;
    }

    void _LoopContext::cancelAllQueued()
    {
        ut_foreach_(ManagedAction *action, mQueuedActions) {
            action->isCancelled = true;
        }
    }

    void _LoopContext::cancelAllPending() // must have lock
    {
        ut_foreach_(ManagedAction *action, mPendingActions) {
            delete action;
        }
        mPendingActions.clear();
    }

    Ticket _LoopContext::scheduleImpl(RepeatingAction&& action, Timepoint triggerTime, long interval, bool catchUp)
    {
        ManagedAction *sa = new ManagedAction(++mTicketCounter, std::move(action), interval, catchUp);
        sa->triggerTime = triggerTime;

        mPendingActions.push_back(sa);

        return sa->ticket;
    }
}

}
