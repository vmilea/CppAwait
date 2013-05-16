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

#pragma once

#include "../Config.h"
#include "Assert.h"
#include "Log.h"

namespace ut {

template <typename T>
class ScopeGuard
{
public:
    typedef ScopeGuard<T> type;

    explicit ScopeGuard(T cleanup)
        : mIsDismissed(false)
        , mCleanup(std::move(cleanup)) { }

    ~ScopeGuard()
    {
        if (!mIsDismissed) {
            try {
                mCleanup();
            } catch (const std::exception& ex) {
                (void) ex;
                ut_assert_msg_(false, "ScopeGuard caught a %s exception: %s", typeid(ex).name(), ex.what());
            } catch (...) {
                ut_assert_msg_(false, "ScopeGuard caught exception");
            }
        }
    }

    void dismiss() const
    {
        mIsDismissed = true;
    }

    bool isDismissed() const
    {
        return mIsDismissed;
    }

    void touch() const  { /* avoids "variable unused" compiler warnings */ }

    ScopeGuard(const type& other); // delete
    type& operator=(const type& other); // delete
    
private:
    void* operator new(size_t size); // must live on stack

    mutable bool mIsDismissed;
    T mCleanup;
};

template <typename T>
ScopeGuard<T> makeScopeGuard(T cleanup)
{
    return ScopeGuard<T>(cleanup);
}

}

//
// ScopeGuards need only be created though these macros
//

#define ut_scope_guard_(cleanup) \
    const auto& ut_anonymous_variable_(scopeGuard) = ut::makeScopeGuard(cleanup); \
    ut_anonymous_variable_(scopeGuard).touch()

#define ut_named_scope_guard_(name, cleanup) \
    const auto& name = ut::makeScopeGuard(cleanup)
