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
#include <CppAwait/Coro.h>
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

static Coro *sMainCoro = nullptr;
static Coro *sCurrentCoro = nullptr;

static std::exception_ptr *sForcedUnwindPtr = nullptr;
static std::exception_ptr *sYieldForbiddenPtr = nullptr;


//
// master/current coroutine
//

void initCoro()
{
    // must be called from main stack

    if (sMainCoro != nullptr) {
        return;
    }

    sMainCoro = new Coro();
    sCurrentCoro = sMainCoro;

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

Coro* masterCoro()
{
    if (sCurrentCoro == nullptr) {
        initCoro();
    }

    return sMainCoro;
}

Coro* currentCoro()
{
    if (sCurrentCoro == nullptr) {
        initCoro();
    }

    return sCurrentCoro;
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
            stackSize = Coro::minimumStackSize();
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
    ut_assert_(sForcedUnwindPtr != nullptr && "not initialized");

    return *sForcedUnwindPtr;
}


//
// YieldForbidden
//

std::exception_ptr YieldForbidden::ptr()
{
    ut_assert_(sYieldForbiddenPtr != nullptr && "not initialized");

    return *sYieldForbiddenPtr;
}


//
// Stack
//

size_t Coro::sDefaultStackSize = 0;

size_t Coro::minimumStackSize()
{
    return StackPool::minimumStackSize();
}

size_t Coro::maximumStackSize()
{
    return StackPool::maximumStackSize();
}

size_t Coro::defaultStackSize()
{
    if (sDefaultStackSize == 0) {
        sDefaultStackSize = StackPool::defaultStackSize();
    }

    return sDefaultStackSize;
}

void Coro::setDefaultStackSize(size_t size)
{
    sDefaultStackSize = size;
}

void Coro::drainStackPool()
{
    sPool.drain();
}

struct Coro::Impl
{
    std::string tag;
    std::pair<void *, size_t> stack;
    ctx::fcontext_t *fc;
    Coro *parent;
    Coro::Func func;
    bool isRunning;
    bool isFullyUnwinded;

    Impl(const std::string& tag, const std::pair<void *, size_t>& stack)
        : tag(tag)
        , stack(stack)
        , fc(nullptr)
        , parent(nullptr)
        , isRunning(false)
        , isFullyUnwinded(true) { }
};

Coro::Coro(const std::string& tag, Func func, size_t stackSize)
    : mImpl(new Impl(tag, sPool.obtain(stackSize)))
{
    if (sMainCoro == nullptr) {
        initCoro();
    }

    ut_log_verbose_("- create coroutine '%s'", mImpl->tag.c_str());

    start(std::move(func));
}

Coro::Coro(const std::string& tag, size_t stackSize)
    : mImpl(new Impl(tag, sPool.obtain(stackSize)))
{
    ut_log_verbose_("- create coroutine '%s'", mImpl->tag.c_str());
}

Coro::Coro()
    : mImpl(new Impl("main", std::pair<void *, size_t>(nullptr, 0)))
{
    ut_log_verbose_("- create coroutine '%s'", mImpl->tag.c_str());

    mImpl->fc = new ctx::fcontext_t();
    mImpl->isRunning = true;
    mImpl->isFullyUnwinded = false;
}

Coro::~Coro()
{
    if (mImpl) {
        ut_log_verbose_("- destroy coroutine '%s'", mImpl->tag.c_str());

        if (this != sMainCoro) {
            ut_assert_(!isRunning() && "can't delete a running coroutine");

            if (!mImpl->isFullyUnwinded) {
                setParent(currentCoro());
                currentCoro()->yieldTo(this);
            }

            sPool.recycle(mImpl->stack.first, mImpl->stack.second);
        } else {
            delete mImpl->fc;
        }
    } else {
        ut_log_verbose_("- destroy moved coroutine");
    }
}

Coro::Coro(Coro&& other)
    : mImpl(std::move(other.mImpl))
{
}

Coro& Coro::operator=(Coro&& other)
{
    if (this != &other) {
        mImpl = std::move(other.mImpl);
    }

    return *this;
}

const char* Coro::tag()
{
    return mImpl->tag.c_str();
}

bool Coro::isRunning()
{
    return mImpl->isRunning;
}

void Coro::start(Func func)
{
    ut_assert_(currentCoro() != this);
    ut_assert_(!isRunning() && "coroutine may not be restarted");

    mImpl->parent = currentCoro();
    mImpl->func = std::move(func);
    mImpl->fc = ctx::make_fcontext(mImpl->stack.first, mImpl->stack.second, &Coro::fcontextFunc);

    mImpl->isRunning = true;
    mImpl->isFullyUnwinded = false;

    currentCoro()->implYieldTo(this, YT_RESULT, this);
}

void* Coro::yield(void *value)
{
    return yieldTo(mImpl->parent, value);
}

void* Coro::yieldTo(Coro *resumeCoro, void *value)
{
    ut_log_info_("- jumping from '%s' to '%s'", sCurrentCoro->tag(), resumeCoro->tag());

    return implYieldTo(resumeCoro, YT_RESULT, value);
}

void* Coro::yieldException(std::exception_ptr eptr)
{
    return yieldExceptionTo(mImpl->parent, eptr);
}

void* Coro::yieldExceptionTo(Coro *resumeCoro, std::exception_ptr eptr)
{
    ut_log_info_("- jumping from '%s' to '%s' (exception)", sCurrentCoro->tag(), resumeCoro->tag());

    return implYieldTo(resumeCoro, YT_EXCEPTION, &eptr);
}

void* Coro::implYieldTo(Coro *resumeCoro, YieldType type, void *value)
{
    ut_assert_(sCurrentCoro == this);
    ut_assert_(resumeCoro != nullptr);
    ut_assert_(resumeCoro != this);
    ut_assert_(!(resumeCoro->mImpl->isFullyUnwinded));

    // ut_log_info_("-- jumping to '%s', type = %s", resumeCoro->tag(), (type == YT_RESULT ? "YT_RESULT" : "YT_EXCEPTION"));

    sCurrentCoro = resumeCoro;

    YieldValue ySent(type, value);
    auto yReceived = (YieldValue *) ctx::jump_fcontext(mImpl->fc, resumeCoro->mImpl->fc, (intptr_t) &ySent);

    // ut_log_info_("-- back to '%s', type = %s", resumeCoro->tag(), (yReceived->type == YT_RESULT ? "YT_RESULT" : "YT_EXCEPTION"));

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

Coro* Coro::parent()
{
    return mImpl->parent;
}

void Coro::setParent(Coro *coro)
{
    ut_assert_(coro != nullptr);
    ut_assert_(coro != this);

    mImpl->parent = coro;
}

void Coro::fcontextFunc(intptr_t data)
{
    auto coro = (Coro *) ((YieldValue *) data)->value;

    std::exception_ptr *peptr = nullptr;
    try {
        void *value = coro->implYieldTo(coro->parent(), YT_RESULT, nullptr);

        ut_log_debug_("- coroutine '%s' func starting", coro->tag());
        coro->mImpl->func(value);
        ut_log_debug_("- coroutine '%s' func done", coro->tag());

    } catch (const ForcedUnwind&) {
        ut_log_debug_("- coroutine '%s' func done (forced unwind)", coro->tag());
    } catch (...) {
        ut_log_debug_("- coroutine '%s' func done (exception)", coro->tag());

        ut_assert_(!std::uncaught_exception() && "may not throw from Coroutine while another exception is propagating");

        std::exception_ptr eptr = std::current_exception();
        ut_assert_(!(eptr == std::exception_ptr()));

        // [MSVC] may not yield from catch block
        peptr = new std::exception_ptr(eptr);
    }

    coro->mImpl->isRunning = false;

    if (peptr != nullptr) {
        // Yielding an exception is trickier because we need to get back here
        // in order to delete the exception_ptr. To make this work the coroutine
        // briefly resumes in destructor if isFullyUnwinded false.

        try {
            coro->yieldException(*peptr);
        } catch (...) {
            ut_assert_(false && "post run exception");
        }

        ut_log_debug_("- coroutine '%s' unwinding", coro->tag());
        delete peptr;
    }

    // all remaining objects on stack have trivial destructors, coroutine if considered fully unwinded

    try {
        coro->mImpl->isFullyUnwinded = true;
        coro->yield();
        ut_assert_(false && "yielded back to fully unwinded coroutine");
    } catch (...) {
        ut_assert_(false && "post run exception");
    }
}

}
