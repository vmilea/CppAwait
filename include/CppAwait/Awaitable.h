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

#pragma once

#include "Config.h"
#include "StackContext.h"
#include "impl/Assert.h"
#include <cstdio>
#include <cstdarg>
#include <string>
#include <memory>
#include <stdexcept>
#include <functional>
#include <vector>
#include <array>

namespace ut {

//
// scheduling hooks, for integration with whatever
// main loop you're using (Qt / GLib / MFC / Asio ...)
//

// Unique ID for a scheduled action. May be used to cancel the action.
//
typedef int Ticket;

// Schedule an action to execute after delay milliseconds.
//
// Note: schedule(0, a), schedule(0, b) implies b cannot run before a.
//
typedef Ticket (*ScheduleDelayedFunc)(long delay, Runnable runnable);

// Cancel action associated with ticket. Returns false if action
// has already executed or if ticket invalid.
//
typedef bool (*CancelScheduledFunc)(Ticket ticket);

// Setup scheduling hooks, this must be done before using Awaitable.
//
void initScheduler(ScheduleDelayedFunc schedule, CancelScheduledFunc cancel);

//
// generic scheduling interface
//

Ticket schedule(Runnable runnable);

Ticket scheduleDelayed(long delay, Runnable runnable);

bool cancelScheduled(Ticket ticket);

//
// Awaitable
//

class Awaitable;

typedef std::unique_ptr<Awaitable> AwaitableHandle;

class Awaitable
{
public:
    typedef std::function<void (Awaitable *awtSelf)> AsyncFunc;
    typedef std::function<void (Awaitable *awt)> OnDoneHandler;

    virtual ~Awaitable();

    void await();

    bool didComplete();

    bool didFail();

    bool isDone();

    void setOnDoneHandler(OnDoneHandler handler);

    const char* tag();

    void setTag(const std::string& tag);

    template<typename T>
    void bindUserData(T *userData, bool takeOwnership = true);

    template<typename T>
    T& userData();

    template<typename T>
    T* userDataPtr();

protected:
    Awaitable();

    void complete();

    void fail(std::exception_ptr eptr);

    std::string mTag;
    StackContext *mBoundContext;
    StackContext *mAwaitingContext;
    Ticket mStartTicket;
    bool mDidComplete;
    std::exception_ptr mExceptionPtr;
    OnDoneHandler mDoneHandler;

    void *mUserData;
    Runnable mUserDataDeleter;

    template <typename Collection>
    friend typename Collection::iterator awaitAny(Collection& awaitables);

    friend AwaitableHandle startAsync(const std::string& tag, AsyncFunc func, size_t stackSize);
};


//
// helpers
//

AwaitableHandle startAsync(const std::string& tag, Awaitable::AsyncFunc func, size_t stackSize = StackContext::defaultStackSize());

AwaitableHandle asyncDelay(long delay);

// Selectors are used by awaitAll/awaitAny to extract Awaitables from Collection.
// You can define your own overloads.

inline Awaitable* selectAwaitable(Awaitable* element)
{
    return element;
}

inline Awaitable* selectAwaitable(AwaitableHandle& element)
{
    return element.get();
}

inline Awaitable* selectAwaitable(AwaitableHandle *element)
{
    return element->get();
}

template <typename First, typename Second>
Awaitable* selectAwaitable(std::pair<First, Second>& element)
{
    return selectAwaitable(element.first);
}

template <typename T>
Awaitable* selectAwaitable(T& element)
{
    return selectAwaitable(element.awaitable);
}

// combinators

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
        if (awt->didComplete()) {
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

    typename Collection::iterator completedPos;
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

// convenience overloads

inline void awaitAll(Awaitable *awt1, Awaitable *awt2)
{
    ut_assert_(awt1 && awt2);

    std::array<Awaitable*, 2> awts = {{ awt1, awt2 }};
    awaitAll(awts);
}

inline void awaitAll(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3)
{
    ut_assert_(awt1 && awt2 && awt3);

    std::array<Awaitable*, 3> awts = {{ awt1, awt2, awt3 }};
    awaitAll(awts);
}

inline void awaitAll(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3, Awaitable *awt4)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4);

    std::array<Awaitable*, 4> awts = {{ awt1, awt2, awt3, awt4 }};
    awaitAll(awts);
}

inline void awaitAll(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3, Awaitable *awt4, Awaitable *awt5)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4 && awt5);

    std::array<Awaitable*, 5> awts = {{ awt1, awt2, awt3, awt4, awt5 }};
    awaitAll(awts);
}

inline void awaitAll(AwaitableHandle& awt1, AwaitableHandle& awt2)
{
    return awaitAll(awt1.get(), awt2.get());
}

inline void awaitAll(AwaitableHandle& awt1, AwaitableHandle& awt2, AwaitableHandle& awt3)
{
    return awaitAll(awt1.get(), awt2.get(), awt3.get());
}

inline void awaitAll(AwaitableHandle& awt1, AwaitableHandle& awt2, AwaitableHandle& awt3, AwaitableHandle& awt4)
{
    return awaitAll(awt1.get(), awt2.get(), awt3.get(), awt4.get());
}

inline void awaitAll(AwaitableHandle& awt1, AwaitableHandle& awt2, AwaitableHandle& awt3, AwaitableHandle& awt4, AwaitableHandle& awt5)
{
    return awaitAll(awt1.get(), awt2.get(), awt3.get(), awt4.get(), awt5.get());
}

inline Awaitable* awaitAny(Awaitable *awt1, Awaitable *awt2)
{
    ut_assert_(awt1 && awt2);

    std::array<Awaitable*, 2> awts = {{ awt1, awt2 }};
    return *awaitAny(awts);
}

inline Awaitable* awaitAny(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3)
{
    ut_assert_(awt1 && awt2 && awt3);

    std::array<Awaitable*, 3> awts = {{ awt1, awt2, awt3 }};
    return *awaitAny(awts);
}

inline Awaitable* awaitAny(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3, Awaitable *awt4)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4);

    std::array<Awaitable*, 4> awts = {{ awt1, awt2, awt3, awt4 }};
    return *awaitAny(awts);
}

inline Awaitable* awaitAny(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3, Awaitable *awt4, Awaitable *awt5)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4 && awt5);

    std::array<Awaitable*, 5> awts = {{ awt1, awt2, awt3, awt4, awt5 }};
    return *awaitAny(awts);
}

inline AwaitableHandle& awaitAny(AwaitableHandle& awt1, AwaitableHandle& awt2)
{
    ut_assert_(awt1 && awt2);

    std::array<AwaitableHandle*, 2> awts = {{ &awt1, &awt2 }};
    return **awaitAny(awts);
}

inline AwaitableHandle& awaitAny(AwaitableHandle& awt1, AwaitableHandle& awt2, AwaitableHandle& awt3)
{
    ut_assert_(awt1 && awt2 && awt3);

    std::array<AwaitableHandle*, 3> awts = {{ &awt1, &awt2, &awt3 }};
    return **awaitAny(awts);
}

inline AwaitableHandle& awaitAny(AwaitableHandle& awt1, AwaitableHandle& awt2, AwaitableHandle& awt3, AwaitableHandle& awt4)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4);

    std::array<AwaitableHandle*, 4> awts = {{ &awt1, &awt2, &awt3, &awt4 }};
    return **awaitAny(awts);
}

inline AwaitableHandle& awaitAny(AwaitableHandle& awt1, AwaitableHandle& awt2, AwaitableHandle& awt3, AwaitableHandle& awt4, AwaitableHandle& awt5)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4 && awt5);

    std::array<AwaitableHandle*, 5> awts = {{ &awt1, &awt2, &awt3, &awt4, &awt5 }};
    return **awaitAny(awts);
}

//
// WhenAll
//

template <typename Collection>
class WhenAll : public Awaitable
{
public:
    WhenAll(Collection& awaitables)
        : mAwaitables(awaitables) { }

    void await()
    {
        if (!mDidComplete) {
            awaitAll(mAwaitables);
            mDidComplete = true;
        }
    }

private:
    Collection& mAwaitables;
};

//
// WhenAny
//

template <typename Collection>
class WhenAny : public Awaitable
{
public:
    WhenAny(Collection& awaitables)
        : mAwaitables(awaitables) { }

    void await()
    {
        if (!mDidComplete) {
            awaitAny(mAwaitables);
            mDidComplete = true;
        }
    }

private:
    Collection& mAwaitables;
};

//
// Completable - exposes complete/fail
//

class Completable : public Awaitable
{
public:
    Completable() { }

    void complete()
    {
        Awaitable::complete();
    }

    void fail(std::exception_ptr eptr)
    {
        Awaitable::fail(eptr);
    }
};

//
// impl
//

template<typename T>
void Awaitable::bindUserData(T *userData, bool takeOwnership)
{
    ut_assert_(mUserData == nullptr);

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
