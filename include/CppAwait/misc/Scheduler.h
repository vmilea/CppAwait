/*
* Copyright 2012-2015 Valentin Milea
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

/**
 * @file  Scheduler.h
 *
 * Declares some generic helpers for scheduling work on the main loop
 * of your program (Qt / GLib / MFC / Asio ...)
 *
 */

#pragma once

#include "../Config.h"
#include "../misc/Functional.h"
#include <memory>

namespace ut {

/**
 * Hook signature -- schedule an action
 * @param action    action to run
 *
 * Note:
 * - action shall not be invoked from within this function
 * - schedule(a), schedule(b) implies a runs before b
 */
typedef void (*ScheduleFunc)(Action action);

/** Setup scheduling hook */
void initScheduler(ScheduleFunc schedule);


//
// generic scheduling interface
//

/** Unique handle for a scheduled action, may be used to cancel the action */
class Ticket
{
public:
    /** Create a dummy ticket */
    Ticket() { }

    /** Move constructor */
    Ticket(Ticket&& other)
        : mAction(std::move(other.mAction)) { }

    /** Move assignment */
    Ticket& operator=(Ticket&& other)
    {
        mAction = std::move(other.mAction);
        return *this;
    }

    /** Cancels action */
    ~Ticket() { }

    /**
     * Check if ticket is attached to an action
     *
     * Returns true unless ticket is dummy or reset. This is unrelated
     * to action having run or not.
     */
    operator bool()
    {
        return mAction.get() != nullptr;
    }

    /** Reset ticket, cancels action */
    void reset()
    {
        mAction.reset();
    }

private:
    Ticket(std::shared_ptr<Action>&& action)
        : mAction(std::move(action)) { }

    Ticket(const Ticket& other); // noncopyable
    Ticket& operator=(const Ticket& other); // noncopyable

    std::shared_ptr<Action> mAction;

    friend Ticket scheduleWithTicket(Action action);
};

/** Schedule an action */
void schedule(Action action);

/** Schedule an action. Supports cancellation: destroying the ticket will implicitly cancel the action */
Ticket scheduleWithTicket(Action action);

}
