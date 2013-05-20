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
#include <CppAwait/Coro.h>
#include <CppAwait/Log.h>
#include <CppAwait/impl/Assert.h>
#include <CppAwait/impl/Foreach.h>
#include <map>
#include <vector>
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

static std::vector<Coro *> sMasterCoroChain;
static Coro *sCurrentCoro = nullptr;

static std::exception_ptr *sForcedUnwindPtr = nullptr;
static std::exception_ptr *sYieldForbiddenPtr = nullptr;


//
// master/current coroutine
//

void initCoroLib()
{
    // must be called from main stack

    ut_assert_(sCurrentCoro == nullptr && "library already initialized");

    Coro *mainCoro = new Coro();

    sMasterCoroChain.push_back(mainCoro);
    sCurrentCoro = mainCoro;

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

Coro* currentCoro()
{
    if (sCurrentCoro == nullptr) {
        initCoroLib();
    }

    return sCurrentCoro;
}

Coro* masterCoro()
{
    if (sCurrentCoro == nullptr) {
        initCoroLib();
    }

    return sMasterCoroChain.back();
}

PushMasterCoro::PushMasterCoro()
{
    if (masterCoro() == sCurrentCoro) {
        mPushedCoro = nullptr;
        return;
    }

    ut_log_verbose_("-- push '%s' as master, replacing '%s'", sCurrentCoro->tag(), masterCoro()->tag());

    sMasterCoroChain.push_back(sCurrentCoro);
    mPushedCoro = sCurrentCoro;
}

PushMasterCoro::~PushMasterCoro()
{
    if (mPushedCoro == nullptr) {
        return;
    }

    if (sMasterCoroChain.back() == mPushedCoro) { // optimize common case
        sMasterCoroChain.pop_back();

        ut_log_verbose_("-- pop '%s', '%s' is now master", mPushedCoro->tag(), masterCoro()->tag());
    } else {
        for (auto it = sMasterCoroChain.end(); it != sMasterCoroChain.begin(); ) {
            --it;

            if (*it == mPushedCoro) {
                sMasterCoroChain.erase(it);

                ut_log_verbose_("-- pop '%s', '%s' is now master", mPushedCoro->tag(), masterCoro()->tag());
                return;
            } else {
                ut_log_verbose_("-- keep '%s'...", (*it)->tag());
            }
        }

        ut_log_warn_("-- couldn't pop '%s' from master coro chain", mPushedCoro->tag());
        ut_assert_(false);
    }
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

    Impl(std::string&& tag, const std::pair<void *, size_t>& stack)
        : tag(std::move(tag))
        , stack(stack)
        , fc(nullptr)
        , parent(nullptr)
        , isRunning(false)
        , isFullyUnwinded(true) { }
};

Coro::Coro(std::string tag, Func func, size_t stackSize)
    : m(new Impl(std::move(tag), sPool.obtain(stackSize)))
{
    if (sCurrentCoro == nullptr) {
        initCoroLib();
    }

    ut_log_verbose_("- new coroutine '%s'", m->tag.c_str());

    init(std::move(func));
}

Coro::Coro(std::string tag, size_t stackSize)
    : m(new Impl(std::move(tag), sPool.obtain(stackSize)))
{
    ut_log_verbose_("- new coroutine '%s'", m->tag.c_str());
}

Coro::Coro()
    : m(new Impl(std::string("main"), std::pair<void *, size_t>(nullptr, 0)))
{
    ut_log_verbose_("- new coroutine '%s'", m->tag.c_str());

    m->fc = new ctx::fcontext_t();
    m->isRunning = true;
    m->isFullyUnwinded = false;
}

Coro::~Coro()
{
    if (m) {
        ut_log_verbose_("- destroy coroutine '%s'", m->tag.c_str());

        if (this != sMasterCoroChain[0]) {
            ut_assert_(!isRunning() && "can't delete a running coroutine");

            if (!m->isFullyUnwinded) {
                setParent(currentCoro());
                currentCoro()->yieldTo(this);
            }

            sPool.recycle(m->stack.first, m->stack.second);
        } else {
            delete m->fc;
        }
    }
}

Coro::Coro(Coro&& other)
    : m(std::move(other.m))
{
}

Coro& Coro::operator=(Coro&& other)
{
    if (this != &other) {
        m = std::move(other.m);
    }

    return *this;
}

const char* Coro::tag()
{
    return m->tag.c_str();
}

bool Coro::isRunning()
{
    return m->isRunning;
}

void Coro::init(Func func)
{
    ut_assert_(currentCoro() != this);
    ut_assert_(!isRunning() && "coroutine already initialized");

    m->parent = currentCoro();
    m->func = std::move(func);
    m->fc = ctx::make_fcontext(m->stack.first, m->stack.second, &Coro::fcontextFunc);

    m->isRunning = true;
    m->isFullyUnwinded = false;
}

void* Coro::yield(void *value)
{
    return yieldTo(m->parent, value);
}

void* Coro::yieldTo(Coro *resumeCoro, void *value)
{
    ut_log_debug_("- '%s' > '%s'", sCurrentCoro->tag(), resumeCoro->tag());

    return implYieldTo(resumeCoro, YT_RESULT, value);
}

void* Coro::yieldException(std::exception_ptr eptr)
{
    return yieldExceptionTo(m->parent, eptr);
}

void* Coro::yieldExceptionTo(Coro *resumeCoro, std::exception_ptr eptr)
{
    ut_log_debug_("- '%s' > '%s' (exception)", sCurrentCoro->tag(), resumeCoro->tag());

    return implYieldTo(resumeCoro, YT_EXCEPTION, &eptr);
}

void* Coro::implYieldTo(Coro *resumeCoro, YieldType type, void *value)
{
    ut_assert_(sCurrentCoro == this);
    ut_assert_(resumeCoro != nullptr);
    ut_assert_(resumeCoro != this);
    ut_assert_(!(resumeCoro->m->isFullyUnwinded));

    // ut_log_debug_("-- jumping to '%s', type = %s", resumeCoro->tag(), (type == YT_RESULT ? "YT_RESULT" : "YT_EXCEPTION"));

    sCurrentCoro = resumeCoro;

    YieldValue ySent(type, value);
    auto yReceived = (YieldValue *) ctx::jump_fcontext(m->fc, resumeCoro->m->fc, (intptr_t) &ySent);

    // ut_log_debug_("-- back to '%s', type = %s", resumeCoro->tag(), (yReceived->type == YT_RESULT ? "YT_RESULT" : "YT_EXCEPTION"));

    return unpackYieldValue(*yReceived);
}

void* Coro::unpackYieldValue(const YieldValue& yReceived)
{
    if (yReceived.type == YT_EXCEPTION) {
        auto exPtr = (std::exception_ptr *) yReceived.value;

        ut_assert_(exPtr != nullptr);
        ut_assert_(!(*exPtr == std::exception_ptr()));

        std::rethrow_exception(*exPtr);
        return nullptr;
    } else {
        ut_assert_(yReceived.type == YT_RESULT);
        return yReceived.value;
    }
}

Coro* Coro::parent()
{
    return m->parent;
}

void Coro::setParent(Coro *coro)
{
    ut_assert_(coro != nullptr);
    ut_assert_(coro != this);

    m->parent = coro;
}

void Coro::fcontextFunc(intptr_t data)
{
    Coro *coro = sCurrentCoro;

    std::exception_ptr *peptr = nullptr;
    try {
        ut_log_debug_("- { '%s'", coro->tag());

        auto yInitial = (YieldValue *) data;
        void *value = coro->unpackYieldValue(*yInitial);
        coro->m->func(value);

        ut_log_debug_("- } '%s'", coro->tag());
    } catch (const ForcedUnwind&) {
        ut_log_debug_("- } '%s' (forced unwind)", coro->tag());
    } catch (...) {
        ut_log_debug_("- } '%s' (exception)", coro->tag());

        ut_assert_(!std::uncaught_exception() && "may not throw from Coroutine while another exception is propagating");

        std::exception_ptr eptr = std::current_exception();
        ut_assert_(!(eptr == std::exception_ptr()));

        // [MSVC] may not yield from catch block
        peptr = new std::exception_ptr(eptr);
    }

    coro->m->isRunning = false;

    if (peptr != nullptr) {
        // Yielding an exception is trickier because we need to get back here
        // in order to delete the exception_ptr. To make this work the coroutine
        // briefly resumes in destructor if isFullyUnwinded false.

        try {
            coro->yieldException(*peptr);
        } catch (...) {
            ut_assert_(false && "post run exception");
        }

        ut_log_debug_("- '%s' unwinding", coro->tag());
        delete peptr;
    }

    // all remaining objects on stack have trivial destructors, coroutine if considered fully unwinded

    try {
        coro->m->isFullyUnwinded = true;
        coro->yield();
        ut_assert_(false && "yielded back to fully unwinded coroutine");
    } catch (...) {
        ut_assert_(false && "post run exception");
    }
}

//
// misc
//

std::exception_ptr ForcedUnwind::ptr()
{
    ut_assert_(sForcedUnwindPtr != nullptr && "not initialized");

    return *sForcedUnwindPtr;
}

std::exception_ptr YieldForbidden::ptr()
{
    ut_assert_(sYieldForbiddenPtr != nullptr && "not initialized");

    return *sYieldForbiddenPtr;
}

void forceUnwind(Coro *coro)
{
    try {
        yieldExceptionTo(coro, ForcedUnwind::ptr());
    } catch (...) {
        ut_assert_(false && "Coro may not throw on ForcedUnwind");
    }
}

}
