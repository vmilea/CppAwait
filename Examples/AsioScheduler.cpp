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
#include "Looper/Thread.h"
#include <CppAwait/AsioWrappers.h>
#include <unordered_map>
#include <memory>
#include <cassert>

class PendingRunnable;

typedef loo::lthread::mutex Mutex;
typedef loo::lthread::lock_guard<Mutex> LockGuard;

static Mutex sMutex;
static std::unordered_map<ut::Ticket, std::unique_ptr<PendingRunnable> > sPendingRunnables;
static ut::Ticket sTicketCounter = 1;

class TicketRunner
{
public:
    TicketRunner(ut::Ticket ticket)
        : mTicket(ticket) { }

    void operator()(const boost::system::error_code& error) const;

private:
    ut::Ticket mTicket;
};

class PendingRunnable
{
public:
    PendingRunnable(ut::Ticket ticket, ut::Runnable runnable, long delay)
        : mTicket(ticket)
        , mRunnable(std::move(runnable))
        , mTimer(ut::asio::io(), boost::posix_time::milliseconds(delay))
    {
        mTimer.async_wait(TicketRunner(mTicket));
    }

    ut::Ticket ticket()
    {
        return mTicket;
    }

    void run()
    {
        mRunnable();
    }

private:
    ut::Ticket mTicket;
    ut::Runnable mRunnable;
    boost::asio::deadline_timer mTimer;
};

inline void TicketRunner::operator()(const boost::system::error_code& error) const
{
    // quirk: deadline_timer doesn't guarantee a non-zero error code
    //        after cancelation, check sPendingRunnables instead

    std::unique_ptr<PendingRunnable> pr;

    { LockGuard _(sMutex);
        auto pos = sPendingRunnables.find(mTicket);

        if (pos != sPendingRunnables.end()) {
            pr = std::move(pos->second);
            sPendingRunnables.erase(pos);
        }
    }

    if (pr) {
        try {
            pr->run();
        } catch (const std::exception& ex) {
            fprintf (stderr, "Action %d has thrown an exception: %s - %s\n", mTicket, typeid(ex).name(), ex.what());
        } catch (...) {
            fprintf (stderr, "Action %d has thrown an exception\n", mTicket);
        }
    }
}

ut::Ticket asioScheduleDelayed(long delay, ut::Runnable runnable)
{
    assert (runnable);

    { LockGuard _(sMutex);
        ut::Ticket ticket = sTicketCounter++;

        sPendingRunnables.insert(std::make_pair(
            ticket,
            std::unique_ptr<PendingRunnable>(new PendingRunnable(ticket, std::move(runnable), delay))));

        return ticket;
    }
}

bool asioCancelScheduled(ut::Ticket ticket)
{
    { LockGuard _(sMutex);
        auto pos = sPendingRunnables.find(ticket);

        if (pos == sPendingRunnables.end()) {
            return false;
        } else {
            sPendingRunnables.erase(pos);
            return true;
        }
    }
}
