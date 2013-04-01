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

#include "../Config.h"
#include <cstdio>
#include <memory>
#include <functional>

namespace ut {

class OpaqueSharedPtr
{
public:
    template <typename T>
    OpaqueSharedPtr(const std::shared_ptr<T>& ref)
        : mHolder(new Holder<T>(ref)) { }

    OpaqueSharedPtr(const OpaqueSharedPtr& other)
        : mHolder(other.mHolder->clone()) { }

    OpaqueSharedPtr& operator=(const OpaqueSharedPtr& other)
    {
        if (this != &other) {
            mHolder.reset(other.mHolder->clone());
        }

        return *this;
    }

    OpaqueSharedPtr(OpaqueSharedPtr&& other)
        : mHolder(std::move(other.mHolder)) { }

    OpaqueSharedPtr& operator=(OpaqueSharedPtr&& other)
    {
        if (this != &other) {
            mHolder = std::move(other.mHolder);
        }

        return *this;
    }

    void clear()
    {
        mHolder.reset();
    }

    long useCount() const
    {
        return mHolder->useCount();
    }

private:
    class HolderBase
    {
    public:
        virtual ~HolderBase() { }

        virtual HolderBase* clone() const = 0;

        virtual long useCount() const = 0;
    };
    
    template <typename T>
    class Holder : public HolderBase
    {
    public:
        Holder(const std::shared_ptr<T>& value)
            : mValue(value) { }

        Holder* clone() const
        {
            return new Holder(mValue);
        }

        long useCount() const
        {
            return mValue.use_count();
        }

    private:
        std::shared_ptr<T> mValue;
    };

    std::unique_ptr<HolderBase> mHolder;
};

}
