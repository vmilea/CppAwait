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

static ScheduleDelayedFunc sScheduleDelayed = nullptr;
static CancelScheduledFunc sCancelScheduled = nullptr;

void initScheduler(ScheduleDelayedFunc schedule, CancelScheduledFunc cancel)
{
    sScheduleDelayed = schedule;
    sCancelScheduled = cancel;
}

Ticket schedule(Runnable runnable)
{
    return sScheduleDelayed(0, std::move(runnable));
}

Ticket scheduleDelayed(long delay, Runnable runnable)
{
    return sScheduleDelayed(delay, std::move(runnable));
}

bool cancelScheduled(Ticket ticket)
{
    return sCancelScheduled(ticket);
}

//
// Awaitable
//

Awaitable::~Awaitable()
{
    ut_log_debug_("* destroy awt '%s', didComplete = %d, didFail = %d", tag(), didComplete(), didFail());

    if (didComplete() || didFail()) {
        ut_assert_(mAwaitingContext == nullptr);
        ut_assert_(mStartTicket == 0);

        if (mBoundContext != nullptr && mBoundContext->isRunning()) {
            ut_log_debug_("* unwinding bound context '%s'", mBoundContext->tag());

            mBoundContext->setParent(currentContext());
            yieldTo(mBoundContext); // resume context to unwind stack
        }
    } else {
        if (mStartTicket != 0) {
            ut_assert_(mAwaitingContext == nullptr);
            cancelScheduled(mStartTicket);
            mStartTicket = 0;
        }
        if (mAwaitingContext != nullptr) {
            // can't print awaiting context tag since it may have been deleted
            ut_log_debug_("* while being awaited");
            mAwaitingContext = nullptr;
        }

        if (mBoundContext != nullptr) {
            ut_log_debug_("* force unwinding of bound context '%s'", mBoundContext->tag());

            if (std::uncaught_exception()) {
                ut_log_debug_("  got uncaught exception");
            }

            ut_assert_(mBoundContext->isRunning());

            mBoundContext->setParent(currentContext());
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
        } else if (mStartTicket == 0) {
            // Bound context already started through main loop. This can happen
            // if we awaited some other Awaitable before this one and the main loop
            // had time to spin.
            yieldTo(mainContext());
        } else {
            // Since we need to yield anyway, bound context can be started
            // immediately instead of going through main loop.
            cancelScheduled(mStartTicket);
            mStartTicket = 0;
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

    if (mDoneHandler) {
        mDoneHandler(this);
    }

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

    if (mDoneHandler) {
        mDoneHandler(this);
    }

    if (mAwaitingContext != nullptr) {
        if (currentContext() != mainContext() && currentContext() != mBoundContext) {
            ut_assert_(false && "called from wrong context");
        }

        yieldTo(mAwaitingContext, this);
    }
}

void Awaitable::setOnDoneHandler(OnDoneHandler handler)
{
    ut_assert_(!mDoneHandler);
    mDoneHandler = handler;
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
    , mStartTicket(0)
    , mDidComplete(false)
    , mUserData(nullptr)
{
    if (sScheduleDelayed == nullptr || sCancelScheduled == nullptr) {
        throw std::runtime_error("Scheduler hooks not setup, you must call initScheduler()");
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

            ut_assert_(!awt->didFail());

            if (!awt->didComplete()) {
                if (awt->mAwaitingContext != nullptr)  {
                    // don't yield on complete, instead wait until context fully unwinded
                    awt->mBoundContext->setParent(awt->mAwaitingContext);
                    awt->mAwaitingContext = nullptr;
                }

                awt->complete(); // mAwaitingContext is null, won't yield
            }
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

        if (!(eptr == std::exception_ptr())) {
            ut_assert_(!awt->didFail());
            ut_assert_(!awt->didComplete());

            if (awt->mAwaitingContext != nullptr)  {
                // don't yield on fail, instead wait until context fully unwinded
                awt->mBoundContext->setParent(awt->mAwaitingContext);
                awt->mAwaitingContext = nullptr;
            }

            awt->fail(eptr);  // mAwaitingContext is null, won't yield
        }

        // This function will never throw an exception. Instead, exceptions
        // are stored in the Awaitable and get rethrown on await().
    }, stackSize);

    context->setParent(mainContext());
    awt->mBoundContext = context;

    awt->mStartTicket = schedule([awt] {
        awt->mStartTicket = 0;
        yieldTo(awt->mBoundContext);
    });

    return AwaitableHandle(awt);
}

AwaitableHandle asyncDelay(long delay)
{
    Completable *awt = new Completable();
    awt->setTag(string_printf("async-delay-%ld", delay));

    Ticket ticket = scheduleDelayed(delay, [delay, awt] {
        awt->complete();
    });

    awt->setOnDoneHandler([ticket](Awaitable *awt) {
        if (awt->didFail()) {
            cancelScheduled(ticket);
        }
    });

    return AwaitableHandle(awt);
}

}
