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
#include <exception>
#include <functional>
#include <vector>

namespace ut {

//
// scheduling hooks, for integration with whatever
// main loop you're using (Qt/GLib/MFC/custom ...)
//

typedef int Ticket;

Ticket scheduleDelayed(long delay, Runnable runnable);

Ticket schedule(Runnable runnable);

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
    
    Collection::iterator completedPos;
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
