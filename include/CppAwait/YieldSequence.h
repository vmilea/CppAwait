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
#include "Coro.h"
#include "impl/Assert.h"
#include <iterator>
#include <memory>

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
        : m(new Impl(std::move(func), Coro("YieldSequence")))
    {
        Impl *impl = m.get();

        m->coro.init([impl](void *startValue) {
            impl->func(startValue);
        });
    }

    ~YieldSequence()
    {
        // TODO: consider invalidating iterators

        if (m && m->coro.isRunning()) {
            forceUnwind(&m->coro);
        }
    }

    /** Move constructor */
    YieldSequence(YieldSequence&& other)
        : m(std::move(other.m))
    {
    }

    /** Move assignment */
    YieldSequence& operator=(YieldSequence&& other)
    {
        // TODO: consider invalidating iterators

        if (m && m->coro.isRunning()) {
            forceUnwind(&m->coro);
        }

        m = std::move(other.m);

        return *this;
    }

    /**
     * Returns a forward iterator
     *
     * May only be called once. Traversing sequence multiple times is not supported.
     */
    iterator begin()
    {
        ut_assert_(m->currentValue == nullptr && "may not begin again");
        ut_assert_(m->coro.isRunning() && "may not begin again");

        Iterator it(m.get());
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
            ut_assert_(mContainer != nullptr && "may not dereference end");
            ut_assert_(mContainer->currentValue != nullptr);
            ut_assert_(mContainer->coro.isRunning());

            return *((T *) mContainer->currentValue);
        }

        Iterator& operator++()
        {
            ut_assert_(mContainer != nullptr && "may not increment past end");

            try {
                mContainer->currentValue = yieldTo(&mContainer->coro);

                if (mContainer->currentValue == nullptr) {
                    // coroutine has finished
                    ut_assert_(!mContainer->coro.isRunning() && "may not yield nullptr from coroutine");
                    mContainer = nullptr;
                }
            } catch (const ForcedUnwind&) { // coroutine interrupted, swallow exception
                mContainer->currentValue = nullptr;
                mContainer = nullptr;
            } catch (...) { // propagate other exceptions thrown by coroutine
                mContainer->currentValue = nullptr;
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
        Iterator(typename YieldSequence<T>::Impl *container)
            : mContainer(container) { }

        Iterator& operator++(int); // disable postfix increment

        typename YieldSequence<T>::Impl *mContainer;

        friend class YieldSequence<T>;
    };

private:
    YieldSequence(const YieldSequence<T>& other); // noncopyable
    YieldSequence<T>& operator=(const YieldSequence<T>& other); // noncopyable

    struct Impl
    {
        Coro::Func func;
        Coro coro;
        void *currentValue;

        Impl(Coro::Func&& func, Coro&& coro)
            : func(std::move(func))
            , coro(std::move(coro))
            , currentValue(nullptr)
        {
        }
    };

    std::unique_ptr<Impl> m;
};

}
