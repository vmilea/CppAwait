/*
* Copyright 2012-2013 Valentin Milea
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

#include "Looper.h"
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
    , mQuit(false)
{
    struct SchedulerAdapter : public AbstractScheduler
    {
    public:
        SchedulerAdapter(Looper& looper)
            : looper(looper) { }

        void schedule(const Action& action)
        {
            looper.schedule(action);
        }

        void schedule(Action&& action)
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
    mThreadId = lthread::this_thread::get_id();

    mQuit = false;
    do {
        { UniqueLock lock(mMutex);
            do {
                Timepoint sleepUntil = mContext.queuePending();
                Timepoint now = getMonotonicTime();

                if (sleepUntil <= now) {
                    break;
                }
                Timepoint::duration timeout = sleepUntil - now;

                if (timeout < lchrono::milliseconds(2)) { // busy wait if less than 2ms until trigger
                    do {
                        mMutex.unlock();
                        lthread::this_thread::yield();
                        mMutex.lock();
                    } while (getMonotonicTime() < sleepUntil && !mContext.hasPending());
                } else {
                    mMutexCond.wait_for(lock, timeout);
                }
            } while (true);
        }

        ut_assert_(!mQuit);
        
        mContext.runQueued(&mQuit);

        lthread::this_thread::yield();
    } while (!mQuit);

    { LockGuard _(mMutex);
        mContext.queuePending(); // delete cancelled actions
    }
}

void Looper::quit()
{
    ut_assert_msg_(lthread::this_thread::get_id() == mThreadId, "%s - quit() called from outside the loop!", mName.c_str());

    cancelAll();
    mQuit = true;
}

bool Looper::cancel(Ticket ticket)
{
    ut_assert_msg_(lthread::this_thread::get_id() == mThreadId, "%s - tryCancel() called from outside the loop!", mName.c_str());

    bool didCancel = mContext.tryCancelQueued(ticket);

    if (!didCancel) {
        { LockGuard _(mMutex);
            didCancel = mContext.tryCancelPending(ticket);
        }
    }

    return didCancel;
}

void Looper::cancelAll()
{
    ut_assert_msg_(lthread::this_thread::get_id() == mThreadId, "%s - cancelAll() called from outside the loop!", mName.c_str());

    mContext.cancelAllQueued();

    { LockGuard _(mMutex);
        mContext.cancelAllPending();
    }
}

//
// detail
//

namespace detail
{
    struct LoopContext::ManagedAction
    {
        ManagedAction(Ticket ticket, RepeatingAction&& action, lchrono::milliseconds interval, bool catchUp)
            : ticket(ticket), action(std::move(action)), interval(interval), catchUp(catchUp), isCancelled(false) { }

        Ticket ticket;
        RepeatingAction action;
        lchrono::milliseconds interval;
        bool catchUp;
        Timepoint triggerTime;
        bool isCancelled;
    };

    LoopContext::LoopContext()
        : mTicketCounter(100)
    {
    }

    LoopContext::~LoopContext()
    {
    }

    void LoopContext::runQueued(bool *quit)
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
                        action->triggerTime += action->interval;
                    } else {
                        action->triggerTime = now + action->interval;
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

    Timepoint LoopContext::queuePending() // must have lock
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

        Timepoint wakeTime = Timepoint::max();

        ut_foreach_(ManagedAction *action, mQueuedActions) {
            if (action->triggerTime < wakeTime) {
                wakeTime = action->triggerTime;
            }
        }

        return wakeTime;
    }

    bool LoopContext::hasPending() // must have lock
    {
        return !mPendingActions.empty();
    }

    bool LoopContext::tryCancelQueued(Ticket ticket)
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

    bool LoopContext::tryCancelPending(Ticket ticket) // must have lock
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

    void LoopContext::cancelAllQueued()
    {
        ut_foreach_(ManagedAction *action, mQueuedActions) {
            action->isCancelled = true;
        }
    }

    void LoopContext::cancelAllPending() // must have lock
    {
        ut_foreach_(ManagedAction *action, mPendingActions) {
            delete action;
        }
        mPendingActions.clear();
    }

    Ticket LoopContext::scheduleImpl(RepeatingAction&& action, Timepoint triggerTime, lchrono::milliseconds interval, bool catchUp)
    {
        ManagedAction *sa = new ManagedAction(++mTicketCounter, std::move(action), interval, catchUp);
        sa->triggerTime = triggerTime;

        mPendingActions.push_back(sa);

        return sa->ticket;
    }
}

}
