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
#include <CppAwait/impl/Util.h>
#include <CppAwait/Awaitable.h>
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

Awaitable::~Awaitable()
{
    const char *reason = (std::uncaught_exception() ? "due to uncaught exception " : "");

    if (didComplete() || didFail()) {
        ut_log_debug_("* destroy awt '%s' %s(%s)", tag(), reason, (didComplete() ? "completed" : "failed"));

        ut_assert_(mAwaitingCoro == nullptr);
    } else {
        ut_log_debug_("* destroy awt '%s' %s(interrupted)", tag(), reason);

        if (mAwaitingCoro != nullptr) {
            // can't print awaiting coroutine tag since it may have been deleted
            // (e.g. a persistent Completable may outlive its awaiter)
            ut_log_info_("*  while being awaited");
            mAwaitingCoro = nullptr;
        }

        if (mBoundCoro != nullptr) {
            ut_log_debug_("*  force bound coroutine '%s' to unwind", mBoundCoro->tag());

            ut_assert_(mBoundCoro->isRunning());

            { PushMasterCoro _; // take over
                // resume coroutine, force fail() via ForcedUnwind
                forceUnwind(mBoundCoro);
            }

            ut_log_debug_("*  unwinded '%s' of awt '%s'", mBoundCoro->tag(), tag());
        } else {
            ut_log_info_("* fail awt '%s'", tag());

            fail(YieldForbidden::ptr());
        }
    }

    if (mBoundCoro != nullptr) {
        ut_assert_(!mBoundCoro->isRunning());
        delete mBoundCoro;
    }

    if (mUserDataDeleter) {
        mUserDataDeleter();
    }
}

void Awaitable::await()
{
    ut_assert_(currentCoro() != masterCoro());
    ut_assert_(mAwaitingCoro == nullptr);

    if (mDidComplete) {
        ut_log_debug_("* await '%s' from '%s' (done)", tag(), currentCoro()->tag());
    } else if (!(mExceptionPtr == std::exception_ptr())) {
        ut_log_debug_("* await '%s' from '%s' (done - exception)", tag(), currentCoro()->tag());

        std::rethrow_exception(mExceptionPtr);
    } else {
        ut_log_debug_("* await '%s' from '%s'", tag(), currentCoro()->tag());

        mAwaitingCoro = currentCoro();
        yieldTo(masterCoro());

        ut_assert_(isDone());
        mAwaitingCoro = nullptr;

        if (!(mExceptionPtr == std::exception_ptr())) {
            std::rethrow_exception(mExceptionPtr);
        }
    }
}

bool Awaitable::didComplete()
{
    return mDidComplete;
}

bool Awaitable::didFail()
{
    return !(mExceptionPtr == std::exception_ptr());
}

bool Awaitable::isDone()
{
    return didComplete() || didFail();
}

void Awaitable::complete()
{
    ut_assert_(!didComplete() && "already complete");
    ut_assert_(!didFail() && "can't complete, already failed");
    mDidComplete = true;

    mOnDone(this);

    if (mAwaitingCoro != nullptr) {
        if (currentCoro() != masterCoro() && currentCoro() != mBoundCoro) {
            ut_assert_(false && "called from wrong coroutine");
        }

        yieldTo(mAwaitingCoro, this);
    }
}

void Awaitable::fail(std::exception_ptr eptr)
{
    ut_assert_(!didFail() && "already failed");
    ut_assert_(!didComplete() && "can't fail, already complete");
    ut_assert_(!(eptr == std::exception_ptr()) && "invalid exception_ptr");
    mExceptionPtr = eptr;

    mOnDone(this);

    if (mAwaitingCoro != nullptr) {
        if (currentCoro() != masterCoro() && currentCoro() != mBoundCoro) {
            ut_assert_(false && "called from wrong coroutine");
        }

        yieldTo(mAwaitingCoro, this);
    }
}

boost::signals2::connection Awaitable::connectToDone(const OnDoneSignal::slot_type& slot)
{
    return mOnDone.connect(slot);
}

std::exception_ptr Awaitable::exception()
{
    return mExceptionPtr;
}

const char* Awaitable::tag()
{
    return mTag.c_str();
}

void Awaitable::setTag(std::string tag)
{
    ut_log_info_("* tag awt '%s'", tag.c_str());

    mTag = std::move(tag);
}

// protected members

Awaitable::Awaitable(std::string tag)
    : mTag(std::move(tag))
    , mBoundCoro(nullptr)
    , mAwaitingCoro(nullptr)
    , mDidComplete(false)
    , mUserData(nullptr)
{
    if (sSchedule == nullptr) {
        throw std::runtime_error("Scheduler hook not setup, you must call initScheduler()");
    }
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
    ut_log_info_("* new awt '%s'", mTag.c_str());
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
    CallbackGuard::Token token = mGuard.getToken();

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
            ut_assert_(!(eptr == std::exception_ptr()));
        }

        ut_assert_(!awt->didFail());
        ut_assert_(!awt->didComplete());

        if (awt->mAwaitingCoro != nullptr) {
            // wait until coroutine fully unwinded before yielding to awaiter
            awt->mBoundCoro->setParent(awt->mAwaitingCoro);
            awt->mAwaitingCoro = nullptr;
        } else {
            // wait until coroutine fully unwinded before yielding to master
            awt->mBoundCoro->setParent(masterCoro());
        }

        if (eptr == std::exception_ptr()) {
            awt->complete(); // mAwaitingCoro is null, won't yield
        } else {
            awt->fail(eptr); // mAwaitingCoro is null, won't yield
        }

        // This function will never throw an exception. Instead, exceptions
        // are stored in the Awaitable and get rethrown by await().
    }, stackSize);

    awt->mBoundCoro = coro;

    { PushMasterCoro _; // take over
        // run coro until it awaits or finishes
        yieldTo(coro);
    }

    return AwaitableHandle(awt);
}

}
