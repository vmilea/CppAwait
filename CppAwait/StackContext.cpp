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
#include <CppAwait/StackContext.h>
#include <CppAwait/impl/Util.h>
#include <map>
#include <algorithm>
#include <cstdio>
#include <boost/context/all.hpp>

namespace ut {
    
namespace ctx = boost::context;

static StackContext *sMainContext = nullptr;
static StackContext *sCurrentContext = nullptr;

// types

struct YieldValue
{
    YieldType type;
    void *value;

    YieldValue(YieldType type, void *value)
        : type(type), value(value) { }
};

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
            stackSize = std::max(minStackSize, StackContext::minimumStackSize());
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

private:
    typedef std::multimap<size_t, void*> StackMap;

    ctx::guarded_stack_allocator mAllocator;
    // ctx::simple_stack_allocator<1048576, 65536, 32768> mAllocator;

    StackMap mStacks;
};

static StackPool sPool;

//
// StackContext
//

size_t StackContext::sDefaultStackSize = 0;

size_t StackContext::minimumStackSize()
{
    return ctx::guarded_stack_allocator::minimum_stacksize();
}

size_t StackContext::defaultStackSize()
{
    if (sDefaultStackSize == 0) {
        sDefaultStackSize = ctx::guarded_stack_allocator::default_stacksize();
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
    bool isUnwinded;
    Runnable postRunAction;
    
    Impl(const std::string& tag, const std::pair<void *, size_t>& stack)
        : tag(tag)
        , stack(stack)
        , fc(nullptr)
        , parent(nullptr)
        , isRunning(false)
        , isUnwinded(true) { }
};

StackContext::StackContext(const std::string& tag, Coroutine coroutine, size_t stackSize)
    : mImpl(new Impl(tag, sPool.obtain(stackSize)))
{
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
    mImpl->isUnwinded = false;
}

StackContext::~StackContext()
{
    ut_log_verbose_("- destroy context '%s'", mImpl->tag.c_str());

    if (mImpl) {
        if (this != sMainContext) {
            ut_assert_(!isRunning() && "can't delete a running context");

            if (!mImpl->isUnwinded) {
                setParent(currentContext());
                currentContext()->yieldTo(this);
            }

            sPool.recycle(mImpl->stack.first, mImpl->stack.second);
        } else {
            delete mImpl->fc;
        }
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

StackContext::operator bool()
{
    return mImpl ? true : false;
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
    mImpl->isUnwinded = false;
    
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
    ut_assert_(!(resumeContext->mImpl->isUnwinded));
    
    sCurrentContext = resumeContext;

    YieldValue ySent(type, value);
    auto yReceived = (YieldValue *) ctx::jump_fcontext(mImpl->fc, resumeContext->mImpl->fc, (intptr_t) &ySent);

    // ut_log_info_("-- back from yield, type = %s", (yReceived->type == YT_RESULT ? "YT_RESULT" : "YT_EXCEPTION"));

    if (yReceived->type == YT_EXCEPTION) {
        auto exPtr = (std::exception_ptr *) yReceived->value;
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
        
        ut_log_debug_("- context '%s' starts running", context->tag());
        context->mImpl->coroutine(value);
        ut_log_debug_("- context '%s' done running", context->tag());
    
    } catch (...) {
        ut_log_debug_("- context '%s' terminated with exception", context->tag());
        
        // [MSVC] may not yield from catch block
        peptr = new std::exception_ptr(std::current_exception());
    }

    context->mImpl->isRunning = false;
    
    if (peptr != nullptr) {
        // Yielding an exception is trickier because we need to get back here
        // in order to delete the exception_ptr. To make this work the context
        // briefly resumes in destructor if isUnwinded false.

        try {
            context->yieldException(*peptr);
        } catch (...) {
            ut_assert_(false && "post run exception");
        }

        ut_log_debug_("- context '%s' unwinding", context->tag());
        delete peptr;
    }

    // all remaining objects on stack have trivial destructors, context considered unwinded

    try {
        context->mImpl->isUnwinded = true;
        context->yield();
        ut_assert_(false && "yielded back to unwinded context");
    } catch (...) {
        ut_assert_(false && "post run exception");
    }
}

//
// main/current context
//

void initMainContext()
{
    ut_assert_(sMainContext == nullptr);

    sMainContext = new StackContext();
    sCurrentContext = sMainContext;
}

StackContext* mainContext()
{
    ut_assert_(sMainContext != nullptr);

    return sMainContext;
}

StackContext* currentContext()
{
    ut_assert_(sCurrentContext != nullptr);

    return sCurrentContext;
}

}