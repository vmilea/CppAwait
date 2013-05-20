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

/**
 * @file  Awaitable.h
 *
 * Declares the Awaitable class and related helpers.
 *
 */

#pragma once

#include "Config.h"
#include "Coro.h"
#include "misc/Signals.h"
#include "misc/CallbackGuard.h"
#include "impl/Assert.h"
#include <array>

namespace ut {

/**
 * @name Scheduling hook
 *
 * Hook for integration with the main loop of your program (Qt / GLib / MFC / Asio ...)
 */
///@{

/**
 * Hook signature -- schedule an action
 * @param action    action to run
 *
 * Note:
 * - action shall not be invoked from within this function
 * - schedule(a), schedule(b) implies a runs before b
 */
typedef void (*ScheduleFunc)(Action action);

/** Setup scheduling hook. This must be done before using Awaitable. */
void initScheduler(ScheduleFunc schedule);

//
// generic scheduling interface
//

/** Unique handle for a scheduled action, may be used to cancel the action */
class Ticket
{
public:
    /** Create a dummy ticket */
    Ticket() { }

    /** Move constructor */
    Ticket(Ticket&& other)
        : mAction(std::move(other.mAction)) { }

    /** Move assignment */
    Ticket& operator=(Ticket&& other)
    {
        mAction = std::move(other.mAction);
        return *this;
    }

    /** Cancels action */
    ~Ticket() { }

    /**
     * Check if ticket is attached to an action
     *
     * Returns true unless ticket is dummy or reset. This is unrelated
     * to action having run or not.
     */
    operator bool()
    {
        return mAction.get() != nullptr;
    }

    /** Reset ticket, cancels action */
    void reset()
    {
        mAction.reset();
    }

private:
    Ticket(std::shared_ptr<Action>&& action)
        : mAction(std::move(action)) { }

    Ticket(const Ticket& other); // delete
    Ticket& operator=(const Ticket& other); // delete

    std::shared_ptr<Action> mAction;

    friend Ticket scheduleWithTicket(Action action);
};

/** Schedule an action */
void schedule(Action action);

/** Schedule an action. Supports cancellation: destroying the ticket will implicitly cancel the action */
Ticket scheduleWithTicket(Action action);

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
    typedef Signal1<Awaitable*> OnDoneSignal;

    /** Destructor */
    virtual ~Awaitable();

    /**
     * Suspend current coroutine until done
     *
     * If not yet done, await() yields control to master coroutine. As an optimization,
     * if the Awaitable was created with startAsync() and it has not yet started,
     * control will be yielded directly to its coroutine instead.
     *
     * On successful completion the awaiting coroutine is resumed. Subsequent
     * calls to await() will return immediately.
     * On failure the exception will be raised in the awaiting coroutine (if any).
     * Each subsequent await() will raise the exception again.
     *
     * Note:
     * - must be called from a coroutine (not from main stack)
     * - awaiting from several coroutines at the same time is not supported
     */
    void await();

    /* True if operation has completed successfully */
    bool didComplete();

    /** True if operation has failed */
    bool didFail();

    /** True if completed or failed */
    bool isDone();

    /** Add a custom handler to be called when done */
    SignalConnection connectToDone(const OnDoneSignal::slot_type& slot);

    /** Exception set on fail */
    std::exception_ptr exception();

    /** Identifier for debugging */
    const char* tag();

    /** Sets an identifier for debugging */
    void setTag(std::string tag);

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

    /**
     * Explicitly set continuation coroutine. Enables awaitAny (select / poll) pattern.
     *
     * @param coro   coroutine to yield to after complete() / fail()
     */
    void setAwaitingCoro(Coro *coro);

public:
    /** Protected constructor */
    Awaitable(std::string tag);

    /**
     * To be called on completion; yields to awaiting coroutine if any.
     *
     * Must be called from master coroutine or bound coroutine.
     */
    void complete();

    /**
     * To be called on fail; throws exception on awaiting coroutine if any.
     *
     * Must be called from master coroutine or bound coroutine.
     */
    void fail(std::exception_ptr eptr);

    struct Impl;
    std::unique_ptr<Impl> m;

private:
    Awaitable(const Awaitable&); // noncopyable
    Awaitable& operator=(const Awaitable&); // noncopyable

    void bindRawUserData(void *userData, Action deleter);

    void* rawUserDataPtr();

    template <typename Collection>
    friend typename Collection::iterator awaitAny(Collection& awaitables);

    friend AwaitableHandle startAsync(std::string tag, AsyncFunc func, size_t stackSize);
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
 * This function prepares func to run as a coroutine. It allocates a Coro and
 * returns an Awaitable hooked up to the coroutine. By using startAsync() you
 * never need to deal directly with Coro.
 *
 * Uncaught exceptions from func -- except for ForcedUnwind -- will pop out on
 * awaiting coroutine.
 *
 * If you delete the Awaitable while the func is running (i.e. while it is awaiting
 * some suboperation), the coroutine will resume with a ForcedUnwind exception.
 * It's expected func will exit immediately upon ForcedUnwind, make sure not to
 * ignore it in a catch (...) handler.
 *
 */
AwaitableHandle startAsync(std::string tag, Awaitable::AsyncFunc func, size_t stackSize = Coro::defaultStackSize());


/**
 * @name Awaitable selectors
 *
 * Attribute shims that are used by awaitAll() / awaitAny() to extract Awaitables
 * from Collection. You can define your own overloads.
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
    ut_assert_(currentCoro() != masterCoro());

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
    ut_assert_(currentCoro() != masterCoro());

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
        awt->setAwaitingCoro(currentCoro());
    }

    yieldTo(masterCoro());

    auto completedPos = awaitables.end();
    Awaitable *completedAwt = nullptr;

    for (auto it = awaitables.begin(); it != awaitables.end(); ++it) {
        Awaitable *awt = selectAwaitable(*it);
        if (awt == nullptr) {
            continue;
        }
        awt->setAwaitingCoro(nullptr);

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
            yieldTo(masterCoro()); // never complete
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

        CallbackWrapper(const CallbackWrapper<F>& other)
            : mCompletable(other.mCompletable)
            , mGuardToken(other.mGuardToken)
            , mCallback(other.mCallback) { }

        CallbackWrapper<F>& operator=(const CallbackWrapper<F>& other)
        {
            mCompletable = other.mCompletable;
            mGuardToken = other.mGuardToken;
            mCallback = other.mCallback;

            return *this;
        }

        CallbackWrapper(CallbackWrapper<F>&& other)
            : mCompletable(other.mCompletable)
            , mGuardToken(std::move(other.mGuardToken))
            , mCallback(std::move(other.mCallback))
        {
            other.mCompletable = nullptr;
        }

        CallbackWrapper<F>& operator=(CallbackWrapper<F>&& other)
        {
            mCompletable = other.mCompletable;
            other.mCompletable = nullptr;
            mGuardToken = std::move(other.mGuardToken);
            mCallback = std::move(other.mCallback);

            return *this;
        }

    #define UT_CALLBACK_WRAPPER_IMPL(...) \
        if (!mGuardToken.isBlocked()) { \
            std::exception_ptr eptr = mCallback(__VA_ARGS__); \
            \
            if (is(eptr)) { \
                mCompletable->fail(eptr); \
            } else { \
                mCompletable->complete(); \
            } \
        }

        void operator()()
        {
            UT_CALLBACK_WRAPPER_IMPL();
        }

        template <typename Arg1>
        void operator()(Arg1&& arg1)
        {
            UT_CALLBACK_WRAPPER_IMPL(std::forward<Arg1>(arg1));
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
        ut::Completable *mCompletable;
        CallbackGuard::Token mGuardToken;
        F mCallback;
    };

    /** Default constructor */
    Completable();

    /** Create a named completable */
    Completable(std::string tag);

    /**
     * To be called on completion; yields to awaiting coroutine if any
     *
     * Must be called from master coroutine.
     */
    void complete(); // not virtual

    /**
     * To be called on fail; throws exception on awaiting coroutine if any
     *
     * Must be called from master coroutine.
     */
    void fail(std::exception_ptr eptr);  // not virtual

    /**
     * Schedule complete
     *
     * May be called from any coroutine.
     */
    void scheduleComplete();

    /**
     * Schedule fail
     *
     * May be called from any coroutine.
     */
    void scheduleFail(std::exception_ptr eptr);

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
    CallbackGuard::Token getGuardToken();

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
};

//
// impl
//

template<typename T>
void Awaitable::bindUserData(T *userData, bool takeOwnership)
{
    bindRawUserData(userData,
        (takeOwnership
            ? [=]() { delete userData; }
            : Action()));
}

template<typename T>
T& Awaitable::userData()
{
    void *data = rawUserDataPtr();

    ut_assert_(data != nullptr);

    return *reinterpret_cast<T*>(data);
}

template<typename T>
T* Awaitable::userDataPtr()
{
    void *data = rawUserDataPtr();

    return reinterpret_cast<T*>(data);
}

}
