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
#include <CppAwait/impl/SharedFlag.h>
#include <CppAwait/impl/StringUtil.h>
#include <CppAwait/misc/Signals.h>
#include <CppAwait/Log.h>
#include <cstdio>
#include <cstdarg>
#include <boost/pool/singleton_pool.hpp>

namespace ut {

//
// Awaitable
//

struct AwaitableImpl
{
    Awaitable *shell;
    std::string tag;
    Coro *boundCoro;
    Coro *awaitingCoro;
    bool didComplete;
    std::exception_ptr exceptionPtr;
    std::shared_ptr<void *> completerGuard;
    Signal0 onDone;

    AwaitableImpl(std::string&& tag)
        : shell(nullptr)
        , tag(std::move(tag))
        , boundCoro(nullptr)
        , awaitingCoro(nullptr)
        , didComplete(false)
    {
    }
};

typedef boost::singleton_pool<
    AwaitableImpl, sizeof(AwaitableImpl),
    boost::default_user_allocator_new_delete,
    boost::details::pool::null_mutex> AwtImplPool;

Awaitable::Awaitable(std::string tag)
{
    currentCoro(); // ensure library is initialized

    m = (AwaitableImpl *) AwtImplPool::malloc();
    new (m) AwaitableImpl(std::move(tag));
    m->shell = this;
}

Awaitable::~Awaitable()
{
    clear();
}

Awaitable::Awaitable(Awaitable&& other)
{
    m = other.m;
    other.m = nullptr;
    m->shell = this;
}

Awaitable& Awaitable::operator=(Awaitable&& other)
{
    clear();
    m = other.m;
    other.m = nullptr;

    if (m) {
        m->shell = this;
    }

    return *this;
}

void Awaitable::await()
{
    ut_assert_(m->awaitingCoro == nullptr && "already being awaited");

    if (m->didComplete) {
        ut_log_debug_("* await '%s' from '%s' (done)", tag(), currentCoro()->tag());
    } else if (is(m->exceptionPtr)) {
        ut_log_debug_("* await '%s' from '%s' (done - exception)", tag(), currentCoro()->tag());

        std::rethrow_exception(m->exceptionPtr);
    } else {
        ut_log_debug_("* await '%s' from '%s'", tag(), currentCoro()->tag());

        ut_assert_(!isNil() && "completer not taken");
        ut_assert_(currentCoro() != masterCoro() && "awaiting would suspend master coro");

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

std::exception_ptr Awaitable::exception()
{
    return m->exceptionPtr;
}

void Awaitable::then(ut::Action slot)
{
    return m->onDone.connectLite(std::move(slot));
}

Completer Awaitable::takeCompleter()
{
    ut_log_info_("* new  evt-awt '%s'", m->tag.c_str());

    ut_assert_(isNil() && "completer already taken");

    m->completerGuard = allocateSharedFlag(m);

    return Completer(m->completerGuard);
}

bool Awaitable::isNil()
{
    return !isDone() && !m->completerGuard;
}

const char* Awaitable::tag()
{
    return m->tag.c_str();
}

void Awaitable::setTag(std::string tag)
{
    m->tag = std::move(tag);
}

Awaitable::Pointer Awaitable::pointer()
{
    return Pointer(m);
}

void Awaitable::setAwaitingCoro(Coro *coro)
{
    ut_assert_(!isNil() && "completer not taken");

    m->awaitingCoro = coro;
}

void Awaitable::complete()
{
    ut_assert_(!didComplete());
    ut_assert_(!didFail());

    m->didComplete = true;
    m->completerGuard.reset();

    if (m->awaitingCoro == nullptr) {
        m->onDone();
    } else {
        m->onDone();

        if (currentCoro() != masterCoro() && currentCoro() != m->boundCoro) {
            ut_assert_(false && "called from wrong coroutine");
        }

        yieldTo(m->awaitingCoro, this);
    }
}

void Awaitable::fail(std::exception_ptr eptr)
{
    ut_assert_(!didComplete());
    ut_assert_(!didFail());

    ut_assert_(is(eptr) && "invalid exception_ptr");

    m->exceptionPtr = std::move(eptr);
    m->completerGuard.reset();

    if (m->awaitingCoro == nullptr) {
        m->onDone();
    } else {
        m->onDone();

        if (currentCoro() != masterCoro() && currentCoro() != m->boundCoro) {
            ut_assert_(false && "called from wrong coroutine");
        }

        yieldTo(m->awaitingCoro, this);
    }
}

void Awaitable::clear()
{
    if (!m) {
        return; // moved
    }

    if (didComplete() || didFail()) { // is done
        ut_log_debug_("* destroy awt '%s' %s(%s)", tag(),
            (std::uncaught_exception() ? "due to uncaught exception " : ""),
            (didComplete() ? "completed" : "failed"));

        ut_assert_(m->awaitingCoro == nullptr);
    } else if (m->completerGuard) { // not nil
        ut_log_debug_("* destroy awt '%s' %s(interrupted)", tag(),
            (std::uncaught_exception() ? "due to uncaught exception " : ""));

        if (m->awaitingCoro != nullptr) {
            // can't print awaiting coroutine tag since it may have been deleted
            // (e.g. a persistent Awaitable may outlive its awaiter)
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

    m->~AwaitableImpl();
    AwtImplPool::free(m);
    m = nullptr;
}

Awaitable Awaitable::makeCompleted()
{
    Awaitable awt;
    awt.complete();
    return std::move(awt);
}

Awaitable Awaitable::makeFailed(std::exception_ptr eptr)
{
    Awaitable awt;
    awt.fail(std::move(eptr));
    return std::move(awt);
}

Awaitable startAsync(std::string tag, Action func, size_t stackSize)
{
    ut_log_info_("* new coro-awt '%s'", tag.c_str());

    Awaitable awt(tag);

    // coroutine owns completer
    awt.m->completerGuard = allocateSharedFlag();

    auto coro = new Coro(std::move(tag), [func](void *awtImpl) {
        auto m = (AwaitableImpl *) awtImpl;
        std::exception_ptr eptr;

        try {
            func();

            ut_log_info_("* complete coro-awt '%s'", m->shell->tag());
        } catch (const ForcedUnwind&) {
            ut_log_info_("* fail coro-awt '%s' (forced unwind)", m->shell->tag());

            // If an Awaitable is being destroyed during propagation of some exception,
            // and the Awaitable is not yet done, it will interrupt itself via ForcedUnwind.
            // In this case, std::current_exception is unreliable: on MSVC it will return
            // empty inside the catch block of the inner exception. As workaround we use
            // a premade exception_ptr.

            eptr = ForcedUnwind::ptr();
        } catch (...) {
            ut_log_info_("* fail coro-awt '%s' (exception)", m->shell->tag());

            ut_assert_(!std::uncaught_exception() && "may not throw from async coroutine while another exception is propagating");

            eptr = std::current_exception();
            ut_assert_(is(eptr));
        }

        ut_assert_(!m->shell->didFail());
        ut_assert_(!m->shell->didComplete());

        if (m->awaitingCoro != nullptr) {
            // wait until coroutine fully unwinded before yielding to awaiter
            m->boundCoro->setParent(m->awaitingCoro);
            m->awaitingCoro = nullptr;
        } else {
            // wait until coroutine fully unwinded before yielding to master
            m->boundCoro->setParent(masterCoro());
        }

        if (is(eptr)) {
            m->shell->fail(std::move(eptr)); // mAwaitingCoro is null, won't yield
        } else {
            m->shell->complete(); // mAwaitingCoro is null, won't yield
        }

        // This function will never throw an exception. Instead, exceptions
        // are stored in the Awaitable and get rethrown by await().
    }, stackSize);

    awt.m->boundCoro = coro;

    { PushMasterCoro _; // take over
        // run coro until it awaits or finishes
        yieldTo(coro, awt.m);
    }

    return std::move(awt);
}

//
// Pointer
//

Awaitable* Awaitable::Pointer::get() const
{
    return m->shell;
}

//
// Completer
//

Awaitable* Completer::awaitable() const
{
    if (auto strongRef = mRef.lock()) {
        return ((AwaitableImpl *) *strongRef)->shell;
    } else {
        return nullptr;
    }
}

void Completer::complete() const
{
    ut_assert_msg_(currentCoro() == masterCoro(),
        "can't complete from '%s' because '%s' is master coro", currentCoro()->tag(), masterCoro()->tag());

    if (auto strongRef = mRef.lock()) {
        auto shell = ((AwaitableImpl *) *strongRef)->shell;

        ut_log_info_("* complete awt '%s'", shell->tag());
        shell->complete();
    }
}

void Completer::fail(std::exception_ptr eptr) const
{
    ut_assert_msg_(currentCoro() == masterCoro(),
        "can't fail from '%s' because '%s' is master coro", currentCoro()->tag(), masterCoro()->tag());

    if (auto strongRef = mRef.lock()) {
        auto shell = ((AwaitableImpl *) *strongRef)->shell;

        ut_log_info_("* fail awt '%s'", shell->tag());
        shell->fail(std::move(eptr));
    }
}

}
