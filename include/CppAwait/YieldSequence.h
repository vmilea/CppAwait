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
 * @file  YieldSequence.h
 *
 * Declares a utility class for iterating over generators
 *
 */

#pragma once

#include "Config.h"
#include "impl/Assert.h"
#include "Coro.h"
#include <iterator>

#define YS_DONE ((void *) -1)

namespace ut {

/**
 * Adapts a generator coroutine for iteration
 */
template <typename T>
class YieldSequence
{
public:
    class Iterator;
    typedef Iterator iterator;

    /**
     * Wraps coroutine into an iterable sequence
     *
     * @param func   Generator. May yield pointers to T or an exception.
     */
    YieldSequence(Coro::Func func)
        : mCoro("YieldSequence")
        , mCurrentValue(nullptr)
    {
        mCoro.init([=](void *startValue) {
            func(startValue);
            mCurrentValue = YS_DONE;
        });
    }

    ~YieldSequence()
    {
        if (mCurrentValue != YS_DONE) {
            ut_assert_(mCoro.isRunning());
            forceUnwind(&mCoro);
        }
    }

    /** Move constructor */
    YieldSequence(YieldSequence&& other)
        : mCoro(std::move(other.mCoro))
        , mCurrentValue(other.mCurrentValue)
    {
        other.mCurrentValue = YS_DONE;
    }

    /** Move assignment */
    YieldSequence& operator=(YieldSequence&& other)
    {
        if (this != &other) {
            mCoro = std::move(other.mCoro);
            mCurrentValue = other.mCurrentValue;
            other.mCurrentValue = YS_DONE;
        }

        return *this;
    }

    /**
     * Returns a forward iterator
     *
     * May only be called once. Traversing sequence multiple times is not supported.
     */
    iterator begin()
    {
        ut_assert_(mCurrentValue != YS_DONE);

        Iterator it(this);
        return ++it;
    }

    /** Returns sequence end */
    iterator end()
    {
        return Iterator(nullptr);
    }

    /**
     * Forward iterator
     */
    class Iterator
    {
    public:
        typedef std::forward_iterator_tag iterator_category;
        typedef T value_type;
        typedef ptrdiff_t difference_type;
        typedef T* pointer;
        typedef T& reference;

        Iterator()
            : mContainer(nullptr) { }

        T& operator*()
        {
            ut_assert_(mContainer != nullptr);
            ut_assert_(mContainer->mCurrentValue != nullptr);
            ut_assert_(mContainer->mCurrentValue != YS_DONE);

            return *((T *) mContainer->mCurrentValue);
        }

        Iterator& operator++()
        {
            ut_assert_(mContainer != nullptr);
            ut_assert_(mContainer->mCurrentValue != YS_DONE);

            try {
                void *value = yieldTo(&mContainer->mCoro);

                if (mContainer->mCurrentValue == YS_DONE) { // coroutine has finished
                    mContainer = nullptr;
                } else { // coroutine has yielded
                    ut_assert_(value != nullptr && "you may not yield nullptr from coroutine");
                    mContainer->mCurrentValue = value;
                }
            } catch (const ForcedUnwind&) { // coroutine interrupted, swallow exception
                mContainer->mCurrentValue = YS_DONE;
                mContainer = nullptr;
            } catch (...) { // propagate other exceptions thrown by coroutine
                mContainer->mCurrentValue = YS_DONE;
                mContainer = nullptr;
                throw;
            }

            return *this;
        }

        bool operator==(const Iterator& other)
        {
            return mContainer == other.mContainer;
        }

        bool operator!=(const Iterator& other)
        {
            return !(*this == other);
        }

    private:
        Iterator(YieldSequence<T> *container)
            : mContainer(container) { }

        YieldSequence *mContainer;

        friend class YieldSequence<T>;
    };

private:
    Coro mCoro;
    void *mCurrentValue;
};

}
