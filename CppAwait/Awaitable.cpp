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
    const char *status;

    if (didComplete()) {
        status = "completed";
    } else if (didFail()) {
        status = "failed";
    } else {
        status = "interrupted";
    }

    ut_log_debug_("* destroy awt '%s' (%s)", tag(), status);

    if (didComplete() || didFail()) {
        ut_assert_(mAwaitingContext == nullptr);
        ut_assert_(!mStartTicket);
    } else {
        if (mStartTicket) {
            ut_assert_(mAwaitingContext == nullptr);
            mStartTicket.reset(); // cancel action
        }
        if (mAwaitingContext != nullptr) {
            // can't print awaiting context tag since it may have been deleted
            // (e.g. a persistent Completable may outlive its awaiter)
            ut_log_debug_("* while being awaited");
            mAwaitingContext = nullptr;
        }

        if (mBoundContext != nullptr) {
            ut_log_debug_("* force unwinding of bound context '%s'", mBoundContext->tag());

            if (std::uncaught_exception()) {
                ut_log_debug_("  got uncaught exception");
            }

            ut_assert_(mBoundContext->isRunning());

            // override parent to get back here after unwinding bound context
            mBoundContext->setParent(currentContext());
            // resume context, force fail() via ForcedUnwind 
            forceUnwind(mBoundContext);
        } else {
            fail(YieldForbidden::ptr());
        }
    }

    if (mBoundContext != nullptr) {
        ut_assert_(!mBoundContext->isRunning());
        delete mBoundContext;
    }

    if (mUserDataDeleter) {
        mUserDataDeleter();
    }

    ut_log_debug_("* destroy awt '%s' end", tag());
}

void Awaitable::await()
{
    ut_log_debug_("* context '%s' awaits %s", currentContext()->tag(), tag());

    ut_assert_(currentContext() != mainContext());
    ut_assert_(mAwaitingContext == nullptr);

    if (!(mExceptionPtr == std::exception_ptr())) {
        std::rethrow_exception(mExceptionPtr);
    }

    if (!mDidComplete) {
        mAwaitingContext = currentContext();

        if (mBoundContext == nullptr) {
            // No bound context, go back to main loop.
            yieldTo(mainContext());
        } else if (!mStartTicket) {
            // Bound context already started through main loop. This can happen
            // if we awaited some other Awaitable before this one and the main loop
            // had time to spin.
            yieldTo(mainContext());
        } else {
            // Since we need to yield anyway, bound context can be started
            // immediately instead of going through main loop.
            mStartTicket.reset(); // cancel action
            yieldTo(mBoundContext);
        }

        ut_assert_(isDone());

        mAwaitingContext = nullptr;

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
    ut_log_debug_("* complete awt '%s'", tag());

    ut_assert_(!didComplete() && "already complete");
    ut_assert_(!didFail() && "can't complete, already failed");
    mDidComplete = true;

    mOnDone(this);

    if (mAwaitingContext != nullptr) {
        if (currentContext() != mainContext() && currentContext() != mBoundContext) {
            ut_assert_(false && "called from wrong context");
        }

        yieldTo(mAwaitingContext, this);
    }
}

void Awaitable::fail(std::exception_ptr eptr)
{
    ut_log_debug_("* fail awt '%s'", tag());

    ut_assert_(!didFail() && "already failed");
    ut_assert_(!didComplete() && "can't fail, already complete");
    ut_assert_(!(eptr == std::exception_ptr()) && "invalid exception_ptr");
    mExceptionPtr = eptr;

    mOnDone(this);

    if (mAwaitingContext != nullptr) {
        if (currentContext() != mainContext() && currentContext() != mBoundContext) {
            ut_assert_(false && "called from wrong context");
        }

        yieldTo(mAwaitingContext, this);
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

void Awaitable::setTag(const std::string& tag)
{
    mTag = tag;
}

// protected members

Awaitable::Awaitable()
    : mBoundContext(nullptr)
    , mAwaitingContext(nullptr)
    , mDidComplete(false)
    , mUserData(nullptr)
{
    if (sSchedule == nullptr) {
        throw std::runtime_error("Scheduler hook not setup, you must call initScheduler()");
    }
}

//
// helpers
//

AwaitableHandle startAsync(const std::string& tag, Awaitable::AsyncFunc func, size_t stackSize)
{
    auto awt = new Awaitable();
    awt->setTag(tag);

    ut_log_debug_("* starting awt '%s'", tag.c_str());

    StackContext *context = new StackContext(tag, [awt, func](void *) {
        std::exception_ptr eptr;

        try {
            func(awt);

            ut_log_debug_("* awt '%s' done", awt->tag());
        } catch (const ForcedUnwind&) {
            ut_log_debug_("* awt '%s' done (forced unwind)", awt->tag());

            // If an Awaitable is being destroyed during propagation of some exception,
            // and the Awaitable is not yet done, it will interrupt itself via ForcedUnwind.
            // In this case, std::current_exception is unreliable: on MSVC it will return
            // empty inside the catch block of the inner exception. As workaround we use
            // a premade exception_ptr.

            eptr = ForcedUnwind::ptr();
        } catch (...) {
            ut_log_debug_("* awt '%s' done (exception)", awt->tag());

            ut_assert_(!std::uncaught_exception() && "may not throw from AsyncFunc while another exception is propagating");

            eptr = std::current_exception();
            ut_assert_(!(eptr == std::exception_ptr()));
        }

        ut_assert_(!awt->didFail());
        ut_assert_(!awt->didComplete());

		if (awt->mAwaitingContext != nullptr) {
            // wait until context fully unwinded before yielding to awaiting context
            awt->mBoundContext->setParent(awt->mAwaitingContext);
            awt->mAwaitingContext = nullptr;
        }

        if (eptr == std::exception_ptr()) {
            awt->complete(); // mAwaitingContext is null, won't yield
        } else {
            awt->fail(eptr); // mAwaitingContext is null, won't yield
        }

        // This function will never throw an exception. Instead, exceptions
        // are stored in the Awaitable and get rethrown by await().
    }, stackSize);

    context->setParent(mainContext());
    awt->mBoundContext = context;

    // defer start until current context suspends itself
    awt->mStartTicket = scheduleWithTicket([awt] {
        awt->mStartTicket.reset();
        yieldTo(awt->mBoundContext);
    });

    return AwaitableHandle(awt);
}

}
