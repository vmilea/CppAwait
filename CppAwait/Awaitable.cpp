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
#include <CppAwait/Awaitable.h>
#include <CppAwait/Log.h>
#include <cstdio>
#include <cstdarg>

namespace ut {

//
// scheduler
//

// runs an action unless it has been canceled
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
        mAction = other.mAction;
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
    sSchedule(std::move(action));
}

Ticket scheduleWithTicket(Action action)
{
    auto sharedAction = std::make_shared<Action>(std::move(action));

    sSchedule(WeakAction(sharedAction));

    return Ticket(std::move(sharedAction));
}

//
// Awaitable
//

struct Awaitable::Impl
{
    std::string tag;
    Coro *boundCoro;
    Coro *awaitingCoro;
    bool didComplete;
    std::exception_ptr exceptionPtr;
    Awaitable::OnDoneSignal onDone;
    void *userData;
    Action userDataDeleter;

    Impl(std::string&& tag)
        : tag(std::move(tag))
        , boundCoro(nullptr)
        , awaitingCoro(nullptr)
        , didComplete(false)
        , userData(nullptr)
    {
    }
};

Awaitable::Awaitable(std::string tag)
    : m(new Impl(std::move(tag)))
{
    if (sSchedule == nullptr) {
        throw std::runtime_error("Scheduler hook not setup, you must call initScheduler()");
    }
}

Awaitable::~Awaitable()
{
    const char *reason = (std::uncaught_exception() ? "due to uncaught exception " : "");

    if (didComplete() || didFail()) {
        ut_log_debug_("* destroy awt '%s' %s(%s)", tag(), reason, (didComplete() ? "completed" : "failed"));

        ut_assert_(m->awaitingCoro == nullptr);
    } else {
        ut_log_debug_("* destroy awt '%s' %s(interrupted)", tag(), reason);

        if (m->awaitingCoro != nullptr) {
            // can't print awaiting coroutine tag since it may have been deleted
            // (e.g. a persistent Completable may outlive its awaiter)
            ut_log_info_("*  while being awaited");
            m->awaitingCoro = nullptr;
        }

        if (m->boundCoro != nullptr) {
            ut_log_debug_("*  force bound coroutine '%s' to unwind", m->boundCoro->tag());

            ut_assert_(m->boundCoro->isRunning());

            { PushMasterCoro _; // take over
                // resume coroutine, force fail() via ForcedUnwind
                forceUnwind(m->boundCoro);
            }

            ut_log_debug_("*  unwinded '%s' of awt '%s'", m->boundCoro->tag(), tag());
        } else {
            ut_log_info_("* fail awt '%s'", tag());

            fail(YieldForbidden::ptr());
        }
    }

    if (m->boundCoro != nullptr) {
        ut_assert_(!m->boundCoro->isRunning());
        delete m->boundCoro;
    }

    if (m->userDataDeleter) {
        m->userDataDeleter();
    }
}

void Awaitable::await()
{
    ut_assert_(currentCoro() != masterCoro());
    ut_assert_(m->awaitingCoro == nullptr);

    if (m->didComplete) {
        ut_log_debug_("* await '%s' from '%s' (done)", tag(), currentCoro()->tag());
    } else if (is(m->exceptionPtr)) {
        ut_log_debug_("* await '%s' from '%s' (done - exception)", tag(), currentCoro()->tag());

        std::rethrow_exception(m->exceptionPtr);
    } else {
        ut_log_debug_("* await '%s' from '%s'", tag(), currentCoro()->tag());

        m->awaitingCoro = currentCoro();
        yieldTo(masterCoro());

        ut_assert_(isDone());
        m->awaitingCoro = nullptr;

        if (is(m->exceptionPtr)) {
            std::rethrow_exception(m->exceptionPtr);
        }
    }
}

bool Awaitable::didComplete()
{
    return m->didComplete;
}

bool Awaitable::didFail()
{
    return is(m->exceptionPtr);
}

bool Awaitable::isDone()
{
    return didComplete() || didFail();
}

void Awaitable::complete()
{
    ut_assert_(!didComplete() && "already complete");
    ut_assert_(!didFail() && "can't complete, already failed");
    m->didComplete = true;

    m->onDone(this);

    if (m->awaitingCoro != nullptr) {
        if (currentCoro() != masterCoro() && currentCoro() != m->boundCoro) {
            ut_assert_(false && "called from wrong coroutine");
        }

        yieldTo(m->awaitingCoro, this);
    }
}

void Awaitable::fail(std::exception_ptr eptr)
{
    ut_assert_(!didFail() && "already failed");
    ut_assert_(!didComplete() && "can't fail, already complete");
    ut_assert_(is(eptr) && "invalid exception_ptr");
    m->exceptionPtr = eptr;

    m->onDone(this);

    if (m->awaitingCoro != nullptr) {
        if (currentCoro() != masterCoro() && currentCoro() != m->boundCoro) {
            ut_assert_(false && "called from wrong coroutine");
        }

        yieldTo(m->awaitingCoro, this);
    }
}

SignalConnection Awaitable::connectToDone(const OnDoneSignal::slot_type& slot)
{
    return m->onDone.connect(slot);
}

std::exception_ptr Awaitable::exception()
{
    return m->exceptionPtr;
}

const char* Awaitable::tag()
{
    return m->tag.c_str();
}

void Awaitable::setTag(std::string tag)
{
    ut_log_info_("* tag awt '%s'", tag.c_str());

    m->tag = std::move(tag);
}

void Awaitable::setAwaitingCoro(Coro *coro)
{
    m->awaitingCoro = coro;
}

void Awaitable::bindRawUserData(void *userData, Action deleter)
{
    if (m->userDataDeleter) {
        m->userDataDeleter();
    }

    m->userData = userData;
    m->userDataDeleter = std::move(deleter);
}

void* Awaitable::rawUserDataPtr()
{
    return m->userData;
}

//
// Completable
//

Completable::Completable()
    : Awaitable(std::string())
{
    ut_log_info_("* new awt ''");
}

Completable::Completable(std::string tag)
    : Awaitable(std::move(tag))
{
    ut_log_info_("* new awt '%s'", m->tag.c_str());
}

void Completable::complete()
{
    ut_log_info_("* complete awt '%s'", tag());

    ut_assert_(currentCoro() == masterCoro());

    mGuard.block();
    Awaitable::complete();
}

void Completable::fail(std::exception_ptr eptr)
{
    ut_log_info_("* fail awt '%s'", tag());

    ut_assert_(currentCoro() == masterCoro());

    mGuard.block();
    Awaitable::fail(eptr);
}

void Completable::scheduleComplete()
{
    CallbackGuard::Token token = getGuardToken();

    schedule([this, token]() {
        if (!token.isBlocked()) {
            complete();
        }
    });
}

void Completable::scheduleFail(std::exception_ptr eptr)
{
    CallbackGuard::Token token = getGuardToken();

    schedule([this, token, eptr]() {
        if (!token.isBlocked()) {
            fail(eptr);
        }
    });
}

CallbackGuard::Token Completable::getGuardToken()
{
    return mGuard.getToken();
}

//
// helpers
//

AwaitableHandle startAsync(std::string tag, Awaitable::AsyncFunc func, size_t stackSize)
{
    ut_log_info_("* new coro-awt '%s'", tag.c_str());

    auto awt = new Awaitable(tag);

    Coro *coro = new Coro(std::move(tag), [awt, func](void *) {
        std::exception_ptr eptr;

        try {
            func(awt);

            ut_log_info_("* complete coro-awt '%s'", awt->tag());
        } catch (const ForcedUnwind&) {
            ut_log_info_("* fail coro-awt '%s' (forced unwind)", awt->tag());

            // If an Awaitable is being destroyed during propagation of some exception,
            // and the Awaitable is not yet done, it will interrupt itself via ForcedUnwind.
            // In this case, std::current_exception is unreliable: on MSVC it will return
            // empty inside the catch block of the inner exception. As workaround we use
            // a premade exception_ptr.

            eptr = ForcedUnwind::ptr();
        } catch (...) {
            ut_log_info_("* fail coro-awt '%s' (exception)", awt->tag());

            ut_assert_(!std::uncaught_exception() && "may not throw from AsyncFunc while another exception is propagating");

            eptr = std::current_exception();
            ut_assert_(is(eptr));
        }

        ut_assert_(!awt->didFail());
        ut_assert_(!awt->didComplete());

        if (awt->m->awaitingCoro != nullptr) {
            // wait until coroutine fully unwinded before yielding to awaiter
            awt->m->boundCoro->setParent(awt->m->awaitingCoro);
            awt->m->awaitingCoro = nullptr;
        } else {
            // wait until coroutine fully unwinded before yielding to master
            awt->m->boundCoro->setParent(masterCoro());
        }

        if (is(eptr)) {
            awt->fail(eptr); // mAwaitingCoro is null, won't yield
        } else {
            awt->complete(); // mAwaitingCoro is null, won't yield
        }

        // This function will never throw an exception. Instead, exceptions
        // are stored in the Awaitable and get rethrown by await().
    }, stackSize);

    awt->m->boundCoro = coro;

    { PushMasterCoro _; // take over
        // run coro until it awaits or finishes
        yieldTo(coro);
    }

    return AwaitableHandle(awt);
}

}
