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

namespace ut {

//
// YieldCollection (experimental, used for iterable generators)
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
            try {
                coroutine(startValue);
            } catch (const InterruptedException&) {
            }
        });
    }

    ~YieldCollection()
    {
        if (mContext) {
            if (mCurrentValue == nullptr) {
                yieldTo(&mContext);
            } else {
                yieldExceptionTo(&mContext, InterruptedException());
            }
        }
    }

    YieldCollection(YieldCollection&& other)
        : mContext(std::move(other.mContext))
        , mCurrentValue(other.mCurrentValue)
    {
        other.mCurrentValue = nullptr;
    }

    YieldCollection& operator=(YieldCollection&& other)
    {
        if (this != &other) {
            mContext = std::move(other.mContext);
            mCurrentValue = other.mCurrentValue;
            other.mCurrentValue = nullptr;
        }
    }

    iterator begin()
    {
        ut_assert_(mContext);

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

            return *mContainer->mCurrentValue;
        }

        Iterator& operator++()
        {
            if (mContainer != nullptr) {
                mContainer->mCurrentValue = (T *) yieldTo(&mContainer->mContext);

                if (mContainer->mCurrentValue == nullptr) {
                    mContainer = nullptr;
                }
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
    T *mCurrentValue;
};

}
