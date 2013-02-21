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

#include "AsioScheduler.h"
#include <CppAwait/AsioWrappers.h>
#include <unordered_map>
#include <cassert>

class PendingRunnable;

static std::unordered_map<ut::Ticket, PendingRunnable*> sPendingRunnables;

class PendingRunnableWrapper
{
public:
    PendingRunnableWrapper(PendingRunnable *pendingRunnable)
        : mPendingRunnable(pendingRunnable) { }

    void operator()(const boost::system::error_code& error) const;

private:
    mutable PendingRunnable *mPendingRunnable;
};

class PendingRunnable
{
public:
    PendingRunnable(ut::Runnable runnable, long delay)
        : mRunnable(std::move(runnable))
        , mTimer(ut::asio::io(), boost::posix_time::milliseconds(delay))
    {
        static ut::Ticket sTicketCounter = 1;
        mTicket = sTicketCounter++;

        sPendingRunnables.insert(std::make_pair(mTicket, this));
    }

    ut::Ticket ticket()
    {
        return mTicket;
    }

    void schedule()
    {
        mTimer.async_wait(PendingRunnableWrapper(this));
    }

    void cancel()
    {
        mTimer.cancel();
        die();
    }

    void run()
    {
        mRunnable();
        die();
    }

private:
    void die()
    {
        auto pos = sPendingRunnables.find(mTicket);
        assert (pos != sPendingRunnables.end());
        assert (pos->second == this);

        sPendingRunnables.erase(pos);

        delete this;
    }

    ut::Runnable mRunnable;
    boost::asio::deadline_timer mTimer;
    ut::Ticket mTicket;
};

inline void PendingRunnableWrapper::operator()(const boost::system::error_code& error) const
{
    if (!error) {
        mPendingRunnable->run();
    }
}

ut::Ticket asioScheduleDelayed(long delay, ut::Runnable runnable)
{
    auto pr = new PendingRunnable(std::move(runnable), delay);
    pr->schedule();

    return pr->ticket();
}

bool asioCancelScheduled(ut::Ticket ticket)
{
    auto pos = sPendingRunnables.find(ticket);

    if (pos == sPendingRunnables.end()) {
        return false;
    } else {
        PendingRunnable *pr = pos->second;
        pr->cancel();

        return true;
    }
}
