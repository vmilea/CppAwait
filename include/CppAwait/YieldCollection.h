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
#include "impl/Assert.h"
#include "StackContext.h"
#include <iterator>

#define YC_DONE ((void *) -1)

namespace ut {

//
// YieldCollection (for easier iteration over generators)
//

template <typename T>
class YieldCollection
{
public:
    class Iterator;
    typedef Iterator iterator;

    YieldCollection(StackContext::Coroutine coroutine)
        : mContext("YieldCollection")
        , mCurrentValue(nullptr)
    {
        mContext.start([=](void *startValue) {
            coroutine(startValue);
            mCurrentValue = YC_DONE;
        });
    }

    ~YieldCollection()
    {
        if (mCurrentValue != YC_DONE) {
            ut_assert_(mContext.isRunning());
            forceUnwind(&mContext);
        }
    }

    YieldCollection(YieldCollection&& other)
        : mContext(std::move(other.mContext))
        , mCurrentValue(other.mCurrentValue)
    {
        other.mCurrentValue = YC_DONE;
    }

    YieldCollection& operator=(YieldCollection&& other)
    {
        if (this != &other) {
            mContext = std::move(other.mContext);
            mCurrentValue = other.mCurrentValue;
            other.mCurrentValue = YC_DONE;
        }
    }

    iterator begin()
    {
        ut_assert_(mCurrentValue != YC_DONE);

        Iterator it(this);
        return ++it;
    }

    iterator end()
    {
        return Iterator(nullptr);
    }

    class Iterator
    {
    public:
        typedef std::input_iterator_tag iterator_category;
        typedef T value_type;
        typedef ptrdiff_t difference_type;
        typedef T* pointer;
        typedef T& reference;

        T& operator*()
        {
            ut_assert_(mContainer != nullptr);
            ut_assert_(mContainer->mCurrentValue != nullptr);
            ut_assert_(mContainer->mCurrentValue != YC_DONE);

            return *((T *) mContainer->mCurrentValue);
        }

        Iterator& operator++()
        {
            ut_assert_(mContainer != nullptr);
            ut_assert_(mContainer->mCurrentValue != YC_DONE);

            try {
                void *value = yieldTo(&mContainer->mContext);

                if (mContainer->mCurrentValue == YC_DONE) { // coroutine has finished
                    mContainer = nullptr;
                } else { // coroutine has yielded
                    ut_assert_(value != nullptr && "you may not yield nullptr from coroutine");
                    mContainer->mCurrentValue = value;
                }
            } catch (const ForcedUnwind&) { // coroutine interrupted, swallow exception
                mContainer->mCurrentValue = YC_DONE;
                mContainer = nullptr;
            } catch (...) { // propagate other exceptions thrown by coroutine
                mContainer->mCurrentValue = YC_DONE;
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
        Iterator(YieldCollection<T> *container)
            : mContainer(container) { }

        YieldCollection *mContainer;

        friend class YieldCollection<T>;
    };

private:
    StackContext mContext;
    void *mCurrentValue;
};

}
