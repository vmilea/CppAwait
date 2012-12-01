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

#pragma once

#include <CppAwait/Config.h>
#include "Chrono.h"
#include "Scheduler.h"
#include "Thread.h"
#include <vector>
#include <memory>
#include <utility>

namespace loo {

typedef std::function<bool ()> RepeatingAction;

typedef int Ticket;

//
// detail
//

namespace detail
{
    class _LoopContext
    {
    public:
        _LoopContext();
        ~_LoopContext();

        void runQueued(bool *quit);
        
            Timepoint queuePending(); // must have lock
        
        bool hasPending(); // must have lock

        template <typename Callable>
        Ticket schedule(Callable&& action, Timepoint triggerTime) // must have lock
        {
            // static_assert(std::is_convertible<Callable, std::function<void ()> >::value, "Action signature must be: () -> void");
            static_assert(std::is_void<decltype(action())>::value, "Action must return void! Use scheduleRepeating for repeating actions.");

            return scheduleImpl(asRepeatingAction(std::forward<Callable>(action)), triggerTime, 0, false);
        }

        template <typename Predicate>
        Ticket scheduleRepeating(Predicate&& action, Timepoint triggerTime, long interval, bool catchUp) // must have lock
        {
            // static_assert(std::is_convertible<Predicate, std::function<bool ()> >::value, "Repeating action signature must be: () -> bool");

            return scheduleImpl(std::move(action), triggerTime, interval, catchUp);
        }

        bool tryCancelQueued(Ticket ticket);
        
        bool tryCancelPending(Ticket ticket); // must have lock
        
        void cancelAllQueued();
        
        void cancelAllPending(); // must have lock

    private:
        struct ManagedAction;

        template <typename Callable>
        static RepeatingAction asRepeatingAction(Callable&& callable)
        {
            struct Wrapper
            {
                typedef typename std::decay<Callable>::type RawCallable;

                Wrapper(const RawCallable& callable)
                    : callable(callable) { }

                Wrapper(RawCallable&& callable)
                    : callable(std::move(callable)) { }

                bool operator()()
                {
                    this->callable();
                    return false;
                }

                RawCallable callable;
            };

            return Wrapper(std::forward<Callable>(callable));
        }

        Ticket scheduleImpl(RepeatingAction&& action, Timepoint triggerTime, long interval, bool catchUp);

        int mTicketCounter;

        std::vector<ManagedAction *> mQueuedActions;
        std::vector<ManagedAction *> mPendingActions;
    };
}

//
// Looper
//

class Looper
{
public:
    Looper(const std::string& name);

    ~Looper();

    void run();

    void quit();

    bool cancel(Ticket ticket);

    void cancelAll();

    /** thread safe */
    template <typename Callable>
    Ticket schedule(Callable&& action, long delay = 0)
    {
        Timepoint triggerTime = getMonotonicTime().plusMilli(delay);

        { loo::ScopedLock<FastMutex> _(mMutex);
            mMutex.signal();
            return mContext.schedule(std::forward<Callable>(action), triggerTime);
        }
    }

    /** thread safe */
    template <typename Predicate>
    Ticket scheduleRepeating(Predicate&& action, long delay = 0, long interval = 0, bool catchUp = false)
    {
        Timepoint triggerTime = getMonotonicTime().plusMilli(delay);

        { loo::ScopedLock<FastMutex> _(mMutex);
            mMutex.signal();
            return mContext.scheduleRepeating(std::forward<Predicate>(action), triggerTime, interval, catchUp);
        }

    }

    operator AbstractScheduler&()
    {
        return *mSchedulerAdapter;
    }

private:
    detail::_LoopContext mContext;
    
    FastMutex mMutex;
    
    std::unique_ptr<AbstractScheduler> mSchedulerAdapter;
    
    std::string mName;
    
    uint32_t mThreadId;

    bool mQuit;
};

void setMainLooper(Looper &mainLooper);
    
Looper& mainLooper();

}
