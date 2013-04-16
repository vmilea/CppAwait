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
#include <CppAwait/StackContext.h>
#include <CppAwait/impl/Util.h>
#include <map>
#include <algorithm>
#include <boost/version.hpp>
#include <boost/context/all.hpp>

// guarded stack allocator has moved to boost/coroutine since Boost 1.53
//
#if BOOST_VERSION > 105200
#include <boost/coroutine/stack_allocator.hpp>
#endif

namespace ut {

namespace ctx = boost::context;

static StackContext *sMainContext = nullptr;
static StackContext *sCurrentContext = nullptr;

static std::exception_ptr *sForcedUnwindPtr = nullptr;
static std::exception_ptr *sYieldForbiddenPtr = nullptr;


//
// main/current context
//

void initMainContext()
{
    // must be called from main stack

    if (sMainContext != nullptr) {
        return;
    }

    sMainContext = new StackContext();
    sCurrentContext = sMainContext;

    // make some exception_ptr in advance to avoid problems
    // with std::current_exception() during exception propagation
    //
    // + wiggle around non-deterministic destruction of globals
    //   by leaking on purpose
    //
    sForcedUnwindPtr = new std::exception_ptr(
                ut::make_exception_ptr(ForcedUnwind()));
    sYieldForbiddenPtr = new std::exception_ptr(
                ut::make_exception_ptr(YieldForbidden()));
}

StackContext* mainContext()
{
    if (sCurrentContext == nullptr) {
        initMainContext();
    }

    return sMainContext;
}

StackContext* currentContext()
{
    if (sCurrentContext == nullptr) {
        initMainContext();
    }

    return sCurrentContext;
}


//
// StackPool
//

class StackPool
{
public:
    StackPool() { }

    std::pair<void*, size_t> obtain(size_t minStackSize)
    {
        // take smallest stack that fits requirement. create one if no match.

        void *stackPtr;
        size_t stackSize;

        StackMap::iterator pos = mStacks.lower_bound(minStackSize);

        if (pos == mStacks.end()) {
            stackSize = StackContext::minimumStackSize();
            if (stackSize < minStackSize) {
                stackSize = minStackSize;
            }

            stackPtr = mAllocator.allocate(stackSize);
        } else {
            stackSize = pos->first;
            stackPtr = pos->second;
            mStacks.erase(pos);
        }

        ut_log_verbose_("obtained stack %p of size %ld", stackPtr, (long) stackSize);

        return std::make_pair(stackPtr, stackSize);
    }

    void recycle(void *stackPtr, size_t stackSize)
    {
        ut_log_verbose_("recycled stack %p of size %ld", stackPtr, (long) stackSize);

        mStacks.insert(std::make_pair(stackSize, stackPtr));
    }

    void drain()
    {
        ut_foreach_(auto& pair, mStacks) {
            mAllocator.deallocate(pair.second, pair.first);
        }
        mStacks.clear();
    }

    static size_t maximumStackSize()
    {
        return Allocator::maximum_stacksize();
    }

    static size_t defaultStackSize()
    {
        return Allocator::default_stacksize();
    }

    static size_t minimumStackSize()
    {
        return Allocator::minimum_stacksize();
    }

private:
    typedef std::multimap<size_t, void*> StackMap;

#if BOOST_VERSION == 105200
    typedef ctx::guarded_stack_allocator Allocator;
#else
    typedef boost::coroutines::stack_allocator Allocator;
#endif

    Allocator mAllocator;
    StackMap mStacks;
};

static StackPool sPool;


//
// ForcedUnwind
//

std::exception_ptr ForcedUnwind::ptr()
{
    ut_assert_(sForcedUnwindPtr != NULL && "not initialized");

    return *sForcedUnwindPtr;
}


//
// YieldForbidden
//

std::exception_ptr YieldForbidden::ptr()
{
    ut_assert_(sYieldForbiddenPtr != NULL && "not initialized");

    return *sYieldForbiddenPtr;
}


//
// StackContext
//

size_t StackContext::sDefaultStackSize = 0;

size_t StackContext::minimumStackSize()
{
    return StackPool::minimumStackSize();
}

size_t StackContext::maximumStackSize()
{
    return StackPool::maximumStackSize();
}

size_t StackContext::defaultStackSize()
{
    if (sDefaultStackSize == 0) {
        sDefaultStackSize = StackPool::defaultStackSize();
    }

    return sDefaultStackSize;
}

void StackContext::setDefaultStackSize(size_t size)
{
    sDefaultStackSize = size;
}

void StackContext::drainContextPool()
{
    sPool.drain();
}

struct StackContext::Impl
{
    std::string tag;
    std::pair<void *, size_t> stack;
    ctx::fcontext_t *fc;
    StackContext *parent;
    StackContext::Coroutine coroutine;
    bool isRunning;
    bool isFullyUnwinded;
    Runnable postRunAction;

    Impl(const std::string& tag, const std::pair<void *, size_t>& stack)
        : tag(tag)
        , stack(stack)
        , fc(nullptr)
        , parent(nullptr)
        , isRunning(false)
        , isFullyUnwinded(true) { }
};

StackContext::StackContext(const std::string& tag, Coroutine coroutine, size_t stackSize)
    : mImpl(new Impl(tag, sPool.obtain(stackSize)))
{
    if (sMainContext == nullptr) {
        initMainContext();
    }

    ut_log_verbose_("- create context '%s'", mImpl->tag.c_str());

    start(std::move(coroutine));
}

StackContext::StackContext(const std::string& tag, size_t stackSize)
    : mImpl(new Impl(tag, sPool.obtain(stackSize)))
{
    ut_log_verbose_("- create stack context '%s'", mImpl->tag.c_str());
}

StackContext::StackContext()
    : mImpl(new Impl("main", std::pair<void *, size_t>(nullptr, 0)))
{
    ut_log_verbose_("- create context '%s'", mImpl->tag.c_str());

    mImpl->fc = new ctx::fcontext_t();
    mImpl->isRunning = true;
    mImpl->isFullyUnwinded = false;
}

StackContext::~StackContext()
{
    if (mImpl) {
        ut_log_verbose_("- destroy context '%s'", mImpl->tag.c_str());

        if (this != sMainContext) {
            ut_assert_(!isRunning() && "can't delete a running context");

            if (!mImpl->isFullyUnwinded) {
                setParent(currentContext());
                currentContext()->yieldTo(this);
            }

            sPool.recycle(mImpl->stack.first, mImpl->stack.second);
        } else {
            delete mImpl->fc;
        }
    } else {
        ut_log_verbose_("- destroy moved context");
    }
}

StackContext::StackContext(StackContext&& other)
    : mImpl(std::move(other.mImpl))
{
}

StackContext& StackContext::operator=(StackContext&& other)
{
    if (this != &other) {
        mImpl = std::move(other.mImpl);
    }

    return *this;
}

const char* StackContext::tag()
{
    return mImpl->tag.c_str();
}

bool StackContext::isRunning()
{
    return mImpl->isRunning;
}

void StackContext::start(Coroutine coroutine)
{
    ut_assert_(currentContext() != this);
    ut_assert_(!isRunning() && "context may not be restarted");

    mImpl->parent = currentContext();
    mImpl->coroutine = std::move(coroutine);
    mImpl->fc = ctx::make_fcontext(mImpl->stack.first, mImpl->stack.second, &StackContext::contextFunc);

    mImpl->isRunning = true;
    mImpl->isFullyUnwinded = false;

    currentContext()->implYieldTo(this, YT_RESULT, this);
}

void* StackContext::yield(void *value)
{
    return yieldTo(mImpl->parent, value);
}

void* StackContext::yieldTo(StackContext *resumeContext, void *value)
{
    ut_log_info_("- jumping from '%s' to '%s'", sCurrentContext->tag(), resumeContext->tag());

    return implYieldTo(resumeContext, YT_RESULT, value);
}

void* StackContext::yieldException(std::exception_ptr eptr)
{
    return yieldExceptionTo(mImpl->parent, eptr);
}

void* StackContext::yieldExceptionTo(StackContext *resumeContext, std::exception_ptr eptr)
{
    ut_log_info_("- jumping from '%s' to '%s' (exception)", sCurrentContext->tag(), resumeContext->tag());

    return implYieldTo(resumeContext, YT_EXCEPTION, &eptr);
}

void* StackContext::implYieldTo(StackContext *resumeContext, YieldType type, void *value)
{
    ut_assert_(sCurrentContext == this);
    ut_assert_(resumeContext != nullptr);
    ut_assert_(resumeContext != this);
    ut_assert_(!(resumeContext->mImpl->isFullyUnwinded));

    // ut_log_info_("-- jumping to '%s', type = %s", resumeContext->tag(), (type == YT_RESULT ? "YT_RESULT" : "YT_EXCEPTION"));

    sCurrentContext = resumeContext;

    YieldValue ySent(type, value);
    auto yReceived = (YieldValue *) ctx::jump_fcontext(mImpl->fc, resumeContext->mImpl->fc, (intptr_t) &ySent);

    // ut_log_info_("-- back to '%s', type = %s", sCurrentContext->tag(), (yReceived->type == YT_RESULT ? "YT_RESULT" : "YT_EXCEPTION"));

    if (yReceived->type == YT_EXCEPTION) {
        auto exPtr = (std::exception_ptr *) yReceived->value;

        ut_assert_(exPtr != nullptr);
        ut_assert_(!(*exPtr == std::exception_ptr()));

        std::rethrow_exception(*exPtr);
        return nullptr;
    } else {
        ut_assert_(yReceived->type == YT_RESULT);
        return yReceived->value;
    }
}

StackContext* StackContext::parent()
{
    return mImpl->parent;
}

void StackContext::setParent(StackContext *context)
{
    ut_assert_(context != nullptr);
    ut_assert_(context != this);

    mImpl->parent = context;
}

void StackContext::contextFunc(intptr_t data)
{
    auto context = (StackContext *) ((YieldValue *) data)->value;

    std::exception_ptr *peptr = nullptr;
    try {
        void *value = context->implYieldTo(context->parent(), YT_RESULT, nullptr);

        ut_log_debug_("- context '%s' func starting", context->tag());
        context->mImpl->coroutine(value);
        ut_log_debug_("- context '%s' func done", context->tag());

    } catch (const ForcedUnwind&) {
        ut_log_debug_("- context '%s' func done (forced unwind)", context->tag());
    } catch (...) {
        ut_log_debug_("- context '%s' func done (exception)", context->tag());

        ut_assert_(!std::uncaught_exception() && "may not throw from Coroutine while another exception is propagating");

        std::exception_ptr eptr = std::current_exception();
        ut_assert_(!(eptr == std::exception_ptr()));

        // [MSVC] may not yield from catch block
        peptr = new std::exception_ptr(eptr);
    }

    context->mImpl->isRunning = false;

    if (peptr != nullptr) {
        // Yielding an exception is trickier because we need to get back here
        // in order to delete the exception_ptr. To make this work the context
        // briefly resumes in destructor if isFullyUnwinded false.

        try {
            context->yieldException(*peptr);
        } catch (...) {
            ut_assert_(false && "post run exception");
        }

        ut_log_debug_("- context '%s' unwinding", context->tag());
        delete peptr;
    }

    // all remaining objects on stack have trivial destructors, context considered fully unwinded

    try {
        context->mImpl->isFullyUnwinded = true;
        context->yield();
        ut_assert_(false && "yielded back to fully unwinded context");
    } catch (...) {
        ut_assert_(false && "post run exception");
    }
}

}
