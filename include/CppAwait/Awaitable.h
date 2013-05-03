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

/**
 * @file  Awaitable.h
 *
 * Declares the Awaitable class and related helpers.
 *
 */

#pragma once

#include "Config.h"
#include "StackContext.h"
#include "impl/Assert.h"
#include "impl/Signals.h"
#include "impl/CallbackGuard.h"
#include <cstdio>
#include <cstdarg>
#include <string>
#include <memory>
#include <stdexcept>
#include <functional>
#include <vector>
#include <array>

namespace ut {

/**
 * @name Scheduling hooks
 *
 * Hooks for integration with the main loop of your program (Qt / GLib / MFC / Asio ...)
 */
///@{

/** Unique ID for a scheduled action, may be used to cancel an action */
typedef int Ticket;

/** Reserved ticket ID */
static const Ticket NO_TICKET = 0;

/**
 * Hook signature -- schedule a delayed action
 * @param delay     milliseconds to wait before running
 * @param runnable  action to run
 * @return  unique ticket ID
 *
 * Note:
 * - runnable shall not be invoked from within this function even if delay is 0
 * - schedule(0, a), schedule(0, b) implies b cannot run before a
 */
typedef Ticket (*ScheduleDelayedFunc)(long delay, Runnable runnable);

/**
 * Hook signature -- cancel an action
 * @param ticket  ID of a scheduled action
 * @return  false if action has already executed or if ticket invalid
 */
typedef bool (*CancelScheduledFunc)(Ticket ticket);

/** Setup scheduling hooks. This must be done before using Awaitable. */
void initScheduler(ScheduleDelayedFunc schedule, CancelScheduledFunc cancel);

//
// generic scheduling interface
//

/** Schedules an action with delay 0 using registered hook */
Ticket schedule(Runnable runnable);

/** Schedules an action using registered hook */
Ticket scheduleDelayed(long delay, Runnable runnable);

/** Cancels an action using registered hook */
bool cancelScheduled(Ticket ticket);

///@}

//
// Awaitable
//

class Awaitable;

/** Unique reference to an Awaitable */
typedef std::unique_ptr<Awaitable> AwaitableHandle;

/**
 * Wrapper for asynchronous operations
 *
 * Awaitable is an a abstraction for asynchronous operations. It represents a
 * unit of work that is expected to finish at some time in the future.
 *
 * While inside a couroutine it's possible to _await_ for some Awaitable to finish. In
 * the coroutine, await() appears to block until the Awaitable completes or fails.
 * Actually the coroutine is suspended and yields control immediately to the program's
 * main loop, allowing for other work to be done while the asynchronous operation is
 * running.
 *
 * An awaitable operation may be implemented as a coroutine (usually when composing
 * simpler awaitables). Or the Awaitable could be hooked to some task running on an
 * external thread.
 *
 * The Awaitable owns its asynchronous operation. Destroying it must immediately
 * interrupt the operation.
 *
 * @warning Not thread safe. Awaitables are designed for single-threaded use.
 *
 */
class Awaitable
{
public:
    /** Coroutine signature required by startAsync() */
    typedef std::function<void (Awaitable *awtSelf)> AsyncFunc;

    /** Signal emitted on complete / fail */
    typedef boost::signals2::signal<void (Awaitable *awt)> OnDoneSignal;

    /** Destructor */
    virtual ~Awaitable();

    /**
     * Suspend current context until done
     *
     * If not yet done, await() yields control to main context. As an optimization,
     * if the Awaitable was created with startAsync() and it has not yet started,
     * control will be yielded directly to its coroutine instead.
     *
     * On successful completion the awaiting context is resumed. Subsequent
     * calls to await() will return immediately.
     * On failure the exception will be raised in the awaiting context (if any).
     * Each subsequent await() will raise the exception again.
     *
     * Note:
     * - must be called from a coroutine (not from main context)
     * - awaiting from several contexts at the same time is not supported
     */
    void await();

    /* True if operation has completed successfully */
    bool didComplete();

    /** True if operation has failed */
    bool didFail();

    /** True if completed or failed */
    bool isDone();

    /** Add a custom handler to be called when done */
    boost::signals2::connection connectToDone(const OnDoneSignal::slot_type& slot);

    /** Exception set on fail */
    std::exception_ptr exception();

    /** Identifier for debugging */
    const char* tag();

    /** Sets an identifier for debugging */
    void setTag(const std::string& tag);

    /**
     * Associate some custom data with this awaitable
     *
     * @param userData       user data
     * @param takeOwnership  whether the awaitable should delete userData when destroyed
     */
    template<typename T>
    void bindUserData(T *userData, bool takeOwnership = true);

    /** Access user data by reference */
    template<typename T>
    T& userData();

    /** Access user data */
    template<typename T>
    T* userDataPtr();

protected:
    /** Protected constructor */
    Awaitable();

    /**
     * To be called on completion; yields to awaiting context if any.
     *
     * Must be called from main context or bound context.
     */
    void complete();

    /**
     * To be called on fail; throws exception on awaiting context if any.
     *
     * Must be called from main context or bound context.
     */
    void fail(std::exception_ptr eptr);

    std::string mTag;
    StackContext *mBoundContext;
    StackContext *mAwaitingContext;
    Ticket mStartTicket;
    bool mDidComplete;
    std::exception_ptr mExceptionPtr;
    OnDoneSignal mOnDone;

    void *mUserData;
    Runnable mUserDataDeleter;

private:
    Awaitable(const Awaitable&); // noncopyable
    Awaitable& operator=(const Awaitable&); // noncopyable

    template <typename Collection>
    friend typename Collection::iterator awaitAny(Collection& awaitables);

    friend AwaitableHandle startAsync(const std::string& tag, AsyncFunc func, size_t stackSize);
};


//
// helpers
//

/**
 * Schedules a function to run asynchronously
 * @param   tag        awaitable tag
 * @param   func       coroutine function
 * @param   stackSize  size of stack to allocate for coroutine
 * @return  an awaitable for managing the asyncronous operation
 *
 * This function prepares func to run as a coroutine. It allocates a StackContext
 * and returns an Awaitable hooked up to the coroutine. By using startAsync() you
 * never need to deal with StackContext.
 *
 * Uncaught exceptions from func -- except for ForcedUnwind -- will pop out on awaiting
 * context.
 *
 * If you delete the Awaitable while the func is running (i.e. while it is awaiting
 * some suboperation), the coroutine will resume with a ForcedUnwind exception.
 * It's expected func will exit immediately upon ForcedUnwind, make sure not to ignore
 * it in a catch (...) handler.
 *
 */
AwaitableHandle startAsync(const std::string& tag, Awaitable::AsyncFunc func, size_t stackSize = StackContext::defaultStackSize());

/** Returns an awaitable that will complete after delay milliseconds */
AwaitableHandle asyncDelay(long delay);


/**
 * @name Awaitable selectors
 *
 * Attribute shims that are used by awaitAll() / awaitAny() to extract Awaitables from Collection. You can define your own overloads.
 */
///@{

/** select Awaitable from an Awaitable */
inline Awaitable* selectAwaitable(Awaitable* element)
{
    return element;
}

/** select Awaitable from an AwaitableHandle */
inline Awaitable* selectAwaitable(AwaitableHandle& element)
{
    return element.get();
}

/** select Awaitable from an AwaitableHandle */
inline Awaitable* selectAwaitable(AwaitableHandle *element)
{
    return element->get();
}

/** select Awaitable from a std::pair */
template <typename First, typename Second>
Awaitable* selectAwaitable(std::pair<First, Second>& element)
{
    return selectAwaitable(element.first);
}

/** select Awaitable from any structure with a field named awaitable */
template <typename T>
Awaitable* selectAwaitable(T& element)
{
    return selectAwaitable(element.awaitable);
}

///@}

// combinators

/**
 * Yield until all awaitables have completed or one of them fails
 * @param awaitables  a collection rom which awaitables can be selected
 *
 * Equivalent to calling await() in sequence for each member of the collection.
 * If any awaitable fails the exception propagates to caller.
 */
template <typename Collection>
void awaitAll(Collection& awaitables)
{
    ut_assert_(currentContext() != mainContext());

    for (auto it = awaitables.begin(); it != awaitables.end(); ++it) {
        Awaitable *awt = selectAwaitable(*it);
        if (awt == nullptr) {
            continue;
        }

        awt->await();
    }
}

/**
 * Yield until any of the awaitables has completed or failed
 * @param awaitables  a collection from which awaitables can be selected
 * @return  iterator to an awaitable that is done
 *
 * Note: If an awaitable fails, the exception is not propagated. You can
 *       trigger it explicitly by awaiting on returned iterator.
 */
template <typename Collection>
typename Collection::iterator awaitAny(Collection& awaitables)
{
    ut_assert_(currentContext() != mainContext());

    bool havePendingAwts = false;

    for (auto it = awaitables.begin(); it != awaitables.end(); ++it) {
        Awaitable *awt = selectAwaitable(*it);
        if (awt == nullptr) {
            continue;
        }
        if (awt->isDone()) {
            return it;
        }
        havePendingAwts = true;
    }
    if (!havePendingAwts) {
        return awaitables.end();
    }

    for (auto it = awaitables.begin(); it != awaitables.end(); ++it) {
        Awaitable *awt = selectAwaitable(*it);
        if (awt == nullptr) {
            continue;
        }
        awt->mAwaitingContext = currentContext();
    }

    yieldTo(mainContext());

    auto completedPos = awaitables.end();
    Awaitable *completedAwt = nullptr;

    for (auto it = awaitables.begin(); it != awaitables.end(); ++it) {
        Awaitable *awt = selectAwaitable(*it);
        if (awt == nullptr) {
            continue;
        }
        awt->mAwaitingContext = nullptr;

        if (completedAwt == nullptr && awt->isDone()) {
            completedAwt = awt;
            completedPos = it;
        }
    }

    ut_assert_(completedAwt != nullptr);
    ut_assert_(completedAwt->isDone());

    return completedPos;
}

/** Compose a collection of awaitables, awaits for all to complete */
template <typename Collection>
AwaitableHandle asyncAll(Collection& awaitables)
{
    return startAsync("asyncAll", [&awaitables](Awaitable* /* awtSelf */) {
        awaitAll(awaitables);
    });
}

/** Compose a collection of awaitables, awaits for any to complete */
template <typename Collection>
AwaitableHandle asyncAny(Collection& awaitables, typename Collection::iterator& pos)
{
    return startAsync("asyncAny", [&awaitables, &pos](Awaitable* /* awtSelf */) {
        if (awaitables.empty()) {
            yieldTo(mainContext()); // never complete
        } else {
            pos = awaitAny(awaitables);
        }
    });
}

// convenience overloads

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(Awaitable *awt1, Awaitable *awt2)
{
    ut_assert_(awt1 && awt2);

    std::array<Awaitable*, 2> awts = {{ awt1, awt2 }};
    awaitAll(awts);
}

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3)
{
    ut_assert_(awt1 && awt2 && awt3);

    std::array<Awaitable*, 3> awts = {{ awt1, awt2, awt3 }};
    awaitAll(awts);
}

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3, Awaitable *awt4)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4);

    std::array<Awaitable*, 4> awts = {{ awt1, awt2, awt3, awt4 }};
    awaitAll(awts);
}

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3, Awaitable *awt4, Awaitable *awt5)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4 && awt5);

    std::array<Awaitable*, 5> awts = {{ awt1, awt2, awt3, awt4, awt5 }};
    awaitAll(awts);
}

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(AwaitableHandle& awt1, AwaitableHandle& awt2)
{
    return awaitAll(awt1.get(), awt2.get());
}

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(AwaitableHandle& awt1, AwaitableHandle& awt2, AwaitableHandle& awt3)
{
    return awaitAll(awt1.get(), awt2.get(), awt3.get());
}

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(AwaitableHandle& awt1, AwaitableHandle& awt2, AwaitableHandle& awt3, AwaitableHandle& awt4)
{
    return awaitAll(awt1.get(), awt2.get(), awt3.get(), awt4.get());
}

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(AwaitableHandle& awt1, AwaitableHandle& awt2, AwaitableHandle& awt3, AwaitableHandle& awt4, AwaitableHandle& awt5)
{
    return awaitAll(awt1.get(), awt2.get(), awt3.get(), awt4.get(), awt5.get());
}

/** Yield until any of the awaitables has completed or failed */
inline Awaitable* awaitAny(Awaitable *awt1, Awaitable *awt2)
{
    ut_assert_(awt1 && awt2);

    std::array<Awaitable*, 2> awts = {{ awt1, awt2 }};
    return *awaitAny(awts);
}

/** Yield until any of the awaitables has completed or failed */
inline Awaitable* awaitAny(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3)
{
    ut_assert_(awt1 && awt2 && awt3);

    std::array<Awaitable*, 3> awts = {{ awt1, awt2, awt3 }};
    return *awaitAny(awts);
}

/** Yield until any of the awaitables has completed or failed */
inline Awaitable* awaitAny(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3, Awaitable *awt4)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4);

    std::array<Awaitable*, 4> awts = {{ awt1, awt2, awt3, awt4 }};
    return *awaitAny(awts);
}

/** Yield until any of the awaitables has completed or failed */
inline Awaitable* awaitAny(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3, Awaitable *awt4, Awaitable *awt5)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4 && awt5);

    std::array<Awaitable*, 5> awts = {{ awt1, awt2, awt3, awt4, awt5 }};
    return *awaitAny(awts);
}

/** Yield until any of the awaitables has completed or failed */
inline AwaitableHandle& awaitAny(AwaitableHandle& awt1, AwaitableHandle& awt2)
{
    ut_assert_(awt1 && awt2);

    std::array<AwaitableHandle*, 2> awts = {{ &awt1, &awt2 }};
    return **awaitAny(awts);
}

/** Yield until any of the awaitables has completed or failed */
inline AwaitableHandle& awaitAny(AwaitableHandle& awt1, AwaitableHandle& awt2, AwaitableHandle& awt3)
{
    ut_assert_(awt1 && awt2 && awt3);

    std::array<AwaitableHandle*, 3> awts = {{ &awt1, &awt2, &awt3 }};
    return **awaitAny(awts);
}

/** Yield until any of the awaitables has completed or failed */
inline AwaitableHandle& awaitAny(AwaitableHandle& awt1, AwaitableHandle& awt2, AwaitableHandle& awt3, AwaitableHandle& awt4)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4);

    std::array<AwaitableHandle*, 4> awts = {{ &awt1, &awt2, &awt3, &awt4 }};
    return **awaitAny(awts);
}

/** Yield until any of the awaitables has completed or failed */
inline AwaitableHandle& awaitAny(AwaitableHandle& awt1, AwaitableHandle& awt2, AwaitableHandle& awt3, AwaitableHandle& awt4, AwaitableHandle& awt5)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4 && awt5);

    std::array<AwaitableHandle*, 5> awts = {{ &awt1, &awt2, &awt3, &awt4, &awt5 }};
    return **awaitAny(awts);
}

/** Exposes complete / fail */
class Completable : public Awaitable
{
public:
    /* Helps wrap asynchronous APIs by hooking the Completable to raw callback */
    template <typename F>
    class CallbackWrapper
    {
    public:
        CallbackWrapper(ut::Completable *completable, F&& callback)
            : mCompletable(completable)
            , mGuardToken(mCompletable->getGuardToken())
            , mCallback(std::move(callback)) { }

        CallbackWrapper(CallbackWrapper<F>&& other)
            : mCompletable(other.mCompletable)
            , mGuardToken(std::move(other.mToken))
            , mCallback(std::move(other.mFunc))
        {
            other.mCompletable = nullptr;
        }

        CallbackWrapper& operator=(CallbackWrapper<F>&& other)
        {
            mCompletable = other.mCompletable;
            other.mCompletable = nullptr;
            mGuardToken = std::move(other.mToken);
            mCallback = std::move(other.mCallback);

            return *this;
        }

    #define UT_CALLBACK_WRAPPER_IMPL(...) \
        if (!mGuardToken.isBlocked()) { \
            std::exception_ptr eptr = mCallback(__VA_ARGS__); \
            \
            if (eptr == std::exception_ptr()) { \
                mCompletable->complete(); \
            } else { \
                mCompletable->fail(eptr); \
            } \
        }

        void operator()()
        {
            UT_CALLBACK_WRAPPER_IMPL();
        }

        template <typename Arg1>
        void operator()(Arg1&& arg1)
        {
            UT_CALLBACK_WRAPPER_IMPL(arg1);
        }

        template <typename Arg1, typename Arg2>
        void operator()(Arg1&& arg1, Arg2&& arg2)
        {
            UT_CALLBACK_WRAPPER_IMPL(std::forward<Arg1>(arg1), std::forward<Arg2>(arg2));
        }

        template <typename Arg1, typename Arg2, typename Arg3>
        void operator()(Arg1&& arg1, Arg2&& arg2, Arg3&& arg3)
        {
            UT_CALLBACK_WRAPPER_IMPL(std::forward<Arg1>(arg1), std::forward<Arg2>(arg2), std::forward<Arg3>(arg3));
        }

        template <typename Arg1, typename Arg2, typename Arg3, typename Arg4>
        void operator()(Arg1&& arg1, Arg2&& arg2, Arg3&& arg3, Arg4&& arg4)
        {
            UT_CALLBACK_WRAPPER_IMPL(std::forward<Arg1>(arg1), std::forward<Arg2>(arg2), std::forward<Arg3>(arg3),
                                     std::forward<Arg4>(arg4));
        }

        template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5>
        void operator()(Arg1&& arg1, Arg2&& arg2, Arg3&& arg3, Arg4&& arg4, Arg5&& arg5)
        {
            UT_CALLBACK_WRAPPER_IMPL(std::forward<Arg1>(arg1), std::forward<Arg2>(arg2), std::forward<Arg3>(arg3),
                                     std::forward<Arg4>(arg4), std::forward<Arg5>(arg5));
        }

    private:
        CallbackWrapper();

        ut::Completable *mCompletable;
        CallbackGuard::Token mGuardToken;
        F mCallback;
    };

    /** Default constructor */
    Completable()
        : mTicket(0) { }

    /** Create a named completable */
    Completable(const std::string& tag)
        : mTicket(0)
    {
        setTag(tag);
    }

    ~Completable()
    {
        if (mTicket != 0) {
            cancelScheduled(mTicket);
        }
    }

    /**
     * To be called on completion; yields to awaiting context if any
     *
     * Must be called from main context or bound context.
     */
    void complete() // not virtual
    {
        // callable only from main context

        ut_assert_(mTicket == 0);
        ut_assert_(currentContext() == mainContext());

        mGuard.block();
        Awaitable::complete();
    }

    /**
     * To be called on fail; throws exception on awaiting context if any
     *
     * Must be called from main context or bound context.
     */
    void fail(std::exception_ptr eptr)  // not virtual
    {
        // callable only from main context

        ut_assert_(mTicket == 0);
        ut_assert_(currentContext() == mainContext());

        mGuard.block();
        Awaitable::fail(eptr);
    }

    /**
     * Schedules complete on main context. May be called from any context.
     */
    void scheduleComplete()
    {
        // callable from any stack context

        if (mTicket == 0) {
            mGuard.block();

            mTicket = schedule([this]() {
                mTicket = 0;
                complete();
            });
        }
    }

    /**
     * Schedules fail on main context. May be called from any context.
     */
    void scheduleFail(std::exception_ptr eptr)
    {
        // callable from any stack context

        if (mTicket == 0) {
            mGuard.block();

            mTicket = schedule([this, eptr]() {
                mTicket = 0;
                fail(eptr);
            });
        }
    }

    /**
     * Returns a token that may be used to check whether the Callable is done
     *
     * The token gets blocked on complete / fail. It is still readable after
     * destroying Callable.
     *
     * Guard tokens help ignore late callbacks, so you don't try to complete
     * a Callable that is no longer valid. Note, wrap() already handles this
     * and is simpler to use.
     */
    CallbackGuard::Token getGuardToken()
    {
        return mGuard.getToken();
    }

    /**
     * Wraps a callback function
     *
     * The wrapper executes func and immediately finishes Callable. Nothing
     * happens if the wrapper runs after Callable is done (and possibly
     * destroyed).
     *
     * @param  func  callback to wrap. Must return a std::exception_ptr
     *               which triggers Completable to complete / fail.
     * @return wrapped func
     */
    template <typename F>
    CallbackWrapper<F> wrap(F func)
    {
        return CallbackWrapper<F>(this, std::move(func));
    }

private:
    CallbackGuard mGuard;
    Ticket mTicket;
};

//
// impl
//

template<typename T>
void Awaitable::bindUserData(T *userData, bool takeOwnership)
{
    if (mUserDataDeleter) {
        mUserDataDeleter();
    }

    mUserData = userData;

    if (takeOwnership) {
        mUserDataDeleter = [=]() { delete userData; };
    }
}

template<typename T>
T& Awaitable::userData()
{
    ut_assert_(mUserData != nullptr);

    return *reinterpret_cast<T*>(mUserData);
}

template<typename T>
T* Awaitable::userDataPtr()
{
    return reinterpret_cast<T*>(mUserData);
}

}
