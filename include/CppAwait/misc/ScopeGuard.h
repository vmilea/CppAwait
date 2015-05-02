/*
* Copyright 2012-2015 Valentin Milea
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
 * @file  ScopeGuard.h
 *
 * Declares the ScopeGuard class.
 *
 */

#pragma once

#include "../Config.h"
#include "../impl/Assert.h"
#include "../Log.h"

namespace ut {

/** Classic scope guard for RAII */
template <typename F>
class ScopeGuard
{
public:
    typedef ScopeGuard<F> type;

    /**
     * Create a dummy scope guard
     */
    explicit ScopeGuard()
        : mIsDismissed(true) { }

    /**
     * Create a scope guard
     * @param cleanup   functor to call at end of scope
     */
    explicit ScopeGuard(const F& cleanup)
        : mIsDismissed(false)
        , mCleanup(cleanup) { }

    /**
     * Create a scope guard
     * @param cleanup   functor to call at end of scope
     */
    explicit ScopeGuard(F&& cleanup)
        : mIsDismissed(false)
        , mCleanup(std::move(cleanup)) { }

    /**
     * Move constructor
     */
    ScopeGuard(ScopeGuard&& other)
        : mIsDismissed(other.mIsDismissed)
        , mCleanup(std::move(other.mCleanup))
    {
        other.mIsDismissed = true;
    }

    /**
     * Move assignment
     */
    ScopeGuard& operator=(ScopeGuard&& other)
    {
        mIsDismissed = other.mIsDismissed;
        other.mIsDismissed = true;
        mCleanup = std::move(other.mCleanup);

        return *this;
    }

    /** Perform cleanup unless dismissed */
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

    /* Dismiss guard */
    void dismiss() const
    {
        mIsDismissed = true;
    }

    /* Check if dismissed */
    bool isDismissed() const
    {
        return mIsDismissed;
    }

    void touch() const  { /* avoids "variable unused" compiler warnings */ }

private:
    ScopeGuard(const ScopeGuard&); // noncopyable
    ScopeGuard& operator=(const ScopeGuard&); // noncopyable

    mutable bool mIsDismissed;
    F mCleanup;
};

/** Create a scope guard with template argument deduction */
template <typename F>
ScopeGuard<F> makeScopeGuard(F cleanup)
{
    return ScopeGuard<F>(cleanup);
}

}

//
// These macros should be enough unless you intend to move ScopeGuard
//

/** Macro for creating anonymous scope guards */
#define ut_scope_guard_(cleanup) \
    const auto& ut_anonymous_variable_(scopeGuard) = ut::makeScopeGuard(cleanup); \
    ut_anonymous_variable_(scopeGuard).touch()

/** Macro for creating named scope guards */
#define ut_named_scope_guard_(name, cleanup) \
    const auto& name = ut::makeScopeGuard(cleanup)
