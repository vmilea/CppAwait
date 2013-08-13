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

#include "ConfigPrivate.h"
#include <CppAwait/misc/Scheduler.h>
#include <CppAwait/impl/Assert.h>

namespace ut {

// runs an action unless it has been canceled
//
class WeakAction
{
public:
    WeakAction() { }

    WeakAction(const std::shared_ptr<Action>& func)
        : mAction(func) { }

    WeakAction(const WeakAction& other)
        : mAction(other.mAction) { }

    WeakAction& operator=(const WeakAction& other)
    {
        if (this != &other) {
            mAction = other.mAction;
        }

        return *this;
    }

    WeakAction(WeakAction&& other)
        : mAction(std::move(other.mAction)) { }

    WeakAction& operator=(WeakAction&& other)
    {
        mAction = std::move(other.mAction);
        return *this;
    }

    void operator()() const
    {
        if (std::shared_ptr<Action> action = mAction.lock()) {
            (*action)();

            // reset functor, otherwise it leaks until Ticket reset
            (*action) = Action();
        }
    }

private:
    std::weak_ptr<Action> mAction;
};


static ScheduleFunc sSchedule = nullptr;

void initScheduler(ScheduleFunc schedule)
{
    sSchedule = schedule;
}

void schedule(Action action)
{
    ut_assert_(sSchedule != nullptr && "scheduler not initialized, call initScheduler()");

    sSchedule(std::move(action));
}

Ticket scheduleWithTicket(Action action)
{
    ut_assert_(sSchedule != nullptr && "scheduler not initialized, call initScheduler()");

    auto sharedAction = std::make_shared<Action>(std::move(action));

    sSchedule(WeakAction(sharedAction));

    return Ticket(std::move(sharedAction));
}

}
