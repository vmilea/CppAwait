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
 * @file  Condition.h
 *
 * Declares the Condition class.
 *
 */

#pragma once

#include "Config.h"
#include "Awaitable.h"
#include "impl/Assert.h"
#include <deque>

namespace ut {

/**
 * Condition variable
 *
 * This is an equivalent of multithreading condition variables. Instead of blocking,
 * you asynchronously wait for condition to get triggered.
 *
 * Several coroutines may be waiting for the condition at the same time. It's possible
 * to notify one or all of them.
 *
 * Notes:
 * - It's fine to immediately call asyncWait() again from awoken coroutine. To avoid
 *   infinite loops, awaitables added during notification are not awaken.
 * - You may call notify recursively (i.e. notify from awoken coroutine)
 * - There are no spurious wakeups
 *
 * @warning Not thread safe. Conditions are designed for single-threaded use.
 *
 */
class Condition
{
public:
    /** Construct a condition */
    Condition(std::string tag = std::string())
        : mTag(tag)
        , mLastWaiterId(0) { }

    /** Identifier for debugging */
    const char* tag()
    {
        return mTag.c_str();
    }

    /** Sets an identifier for debugging */
    void setTag(std::string tag)
    {
        mTag = std::move(tag);
    }

    /** Wait until condition triggered */
    Awaitable asyncWait()
    {
        Awaitable awt;

        mWaiters.push_back(
            Waiter(++mLastWaiterId, awt.takeCompleter()));

        return std::move(awt);
    }

    /**
     * Triggers condition, completes a single async wait
     *
     * The first Awaitable gets completed.
     */
    void notifyOne()
    {
        if (mWaiters.empty()) {
            return;
        }

        { ut::PushMasterCoro _;
            do {
                Completer completer = std::move(mWaiters.front().completer);
                mWaiters.pop_front();

                if (!completer.isExpired()) {
                    completer();
                    break;
                }
            } while (!mWaiters.empty());
        }
    }

    /**
     * Triggers condition, completes all async waits
     *
     * Awaitables get completed in FIFO order.
     */
    void notifyAll()
    {
        if (mWaiters.empty()) {
            return;
        }

        { ut::PushMasterCoro _;
            Waiter::Id maxId = mLastWaiterId;

            ut_assert_(mWaiters.back().id == maxId);
            ut_assert_(mWaiters.front().id <= maxId);

            do {
                Completer completer = std::move(mWaiters.front().completer);
                mWaiters.pop_front();

                completer();
            } while (!mWaiters.empty() && mWaiters.front().id <= maxId);
        }
    }

private:
    struct Waiter
    {
        typedef size_t Id;

        Id id;
        Completer completer;

        Waiter(Id id, Completer&& completer)
            : id(id)
            , completer(std::move(completer)) { }

        Waiter(Waiter&& other)
            : id(std::move(other.id))
            , completer(std::move(other.completer))
        {
        }

        Waiter& operator=(Waiter&& other)
        {
            id = other.id;
            completer = std::move(other.completer);

            return *this;
        }

    private:
        Waiter(const Waiter& other); // noncopyable
        Waiter& operator=(const Waiter& other); // noncopyable
    };

    Condition(const Condition&); // noncopyable
    Condition& operator=(const Condition&); // noncopyable

    std::string mTag;

    Waiter::Id mLastWaiterId;
    std::deque<Waiter> mWaiters;

};

}
