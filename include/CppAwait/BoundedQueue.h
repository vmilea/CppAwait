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
 * @file  BoundedQueue.h
 *
 * Declares the BoundedQueue class.
 *
 */

#pragma once

#include "Config.h"
#include "Condition.h"
#include "impl/Assert.h"
#include <deque>

namespace ut {

/**
 * Asynchronous bounded queue
 *
 * BoundedQueue supports async producer-consumer patterns.
 *
 * @warning Not thread safe. BoundedQueues are designed for single-threaded use.
 */
template <typename T>
struct BoundedQueue
{
public:
    /** Construct a queue that can grow up to maxSize */
    BoundedQueue(size_t maxSize = (size_t) -1)
        : mMaxSize(maxSize) { }

    /** Max queue size */
    size_t maxSize() const
    {
        return mMaxSize;
    }

    /** Queue size */
    size_t size() const
    {
        return mQueue.size();
    }

    /** Check if queue empty */
    bool isEmpty() const
    {
        return mQueue.empty();
    }

    /** Check if queue full */
    bool isFull() const
    {
        return size() == maxSize();
    }

    /**
     * Push a value. Push is performed immediately unless queue full.
     *
     * @param   value    value to push
     * @return  an awaitable that completes after value has been pushed.
     */
    Awaitable asyncPush(T value)
    {
        if (mQueue.size() < mMaxSize) {
            mQueue.push_back(std::move(value));
            mCondPoppable.notifyOne();

            return Awaitable::makeCompleted();
        } else {
            Awaitable awt = mCondPushable.asyncWait();

            awt.connectToDoneLite([this, value](Awaitable *awt) {
                if (!awt->didFail()) {
                    ut_assert_(this->mQueue.size() < this->mMaxSize);

                    this->mQueue.push_back(value);
                    this->mCondPoppable.notifyOne();
                }
            });

            return std::move(awt);
        }
    }

    /**
     * Pop a value. Pop is performed immediately unless queue empty.
     *
     * @param   outValue    holds value, must not be freed before awaitable done
     * @return  an awaitable that completes after value has been popped.
     */
    Awaitable asyncPop(T& outValue)
    {
        if (!mQueue.empty()) {
            outValue = std::move(mQueue.front());
            mQueue.pop_front();
            mCondPushable.notifyOne();

            return Awaitable::makeCompleted();
        } else {
            Awaitable awt = mCondPoppable.asyncWait();

            awt.connectToDoneLite([this, &outValue](Awaitable *awt) {
                if (!awt->didFail()) {
                    ut_assert_(!this->mQueue.empty());

                    outValue = std::move(this->mQueue.front());
                    this->mQueue.pop_front();
                    this->mCondPushable.notifyOne();
                }
            });

            return std::move(awt);
        }
    }

private:
    BoundedQueue(const BoundedQueue<T>&); // noncopyable
    BoundedQueue<T>& operator=(const BoundedQueue<T>&); // noncopyable

    size_t mMaxSize;
    std::deque<T> mQueue;

    Condition mCondPoppable; // not empty
    Condition mCondPushable; // not full
};

}