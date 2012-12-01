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

#include "stdafx.h"
#include <CppAwait/Awaitable.h>
#include <CppAwait/impl/Util.h>
#include <cstdio>
#include <cstdarg>

namespace ut {

//
// Awaitable
//

Awaitable::~Awaitable()
{
    ut_log_debug_("* destroy awt '%s', didComplete = %d, didFail = %d", tag(), didComplete(), didFail());

    if (didComplete() || didFail()) {
        ut_assert_(mAwaitingContext == nullptr);

        if (mBoundContext != nullptr && mBoundContext->isRunning()) {
            ut_log_debug_("* unwinding bound context '%s'", mBoundContext->tag());
            
            mBoundContext->setParent(currentContext());
            yieldTo(mBoundContext); // resume context to unwind stack
        }
    } else {
        if (mAwaitingContext != nullptr) {
            ut_log_debug_("* while being awaited by '%s'", mAwaitingContext->tag());
            mAwaitingContext = nullptr;
        }

        if (mBoundContext != nullptr) {
            ut_log_debug_("* interrupting bound context '%s'", mBoundContext->tag());
            ut_assert_(mBoundContext->isRunning());
            
            mBoundContext->setParent(currentContext());
            try {
                yieldExceptionTo(mBoundContext, InterruptedException()); // interrupt context
            } catch (...) {
            }
        } else {
            fail(make_exception_ptr(InterruptedException()));
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
    ut_assert_(currentContext() != mainContext());
    ut_assert_(mAwaitingContext == nullptr);

    if (!(mExceptionPtr == std::exception_ptr())) {
        std::rethrow_exception(mExceptionPtr);
    }

    if (!mDidComplete) {
        mAwaitingContext = currentContext();

        // yielding to parent would be possible but is clunky
        // e.g: A awaits B
        //      B awaits C1, C2, C3 => A needs to ignore 3 spurious yields from B
        //
        // hence we yield to main context
        void *thiz = yield(mainContext());

        ut_assert_(this == thiz);
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
}

void Awaitable::setBoundContext(StackContext *context)
{
    ut_assert_(mBoundContext == nullptr);
    mBoundContext = context;
}

//
// helpers
//

AwaitableHandle startAsync(const std::string& tag, Awaitable::AsyncFunc func, size_t stackSize)
{
    auto awt = new Completable();
    awt->setTag(tag);

    ut_log_debug_("* starting awt '%s'", tag.c_str());
    
    StackContext *context = new StackContext(tag, [awt, func](void *) {
        std::exception_ptr eptr;
        
        try {
            func(awt);
            ut_assert_(!awt->didFail());

            if (!awt->didComplete()) {
                awt->complete();
            }
        } catch (...) {
            ut_assert_(!awt->didFail());
            ut_assert_(!awt->didComplete());

            // [MSVC] may not yield from catch block
            eptr = std::current_exception();
        }

        if (!(eptr == std::exception_ptr())) {
            awt->fail(eptr);
        }
    }, stackSize);

    context->setParent(mainContext());
    awt->setBoundContext(context);

    // FIXME: awaitable 
    schedule([context] {
        yieldTo(context);
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