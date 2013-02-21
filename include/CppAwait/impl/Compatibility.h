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
#include "Assert.h"
#include <exception>

namespace ut {

// MSVC10 workaround -- no make_exception_ptr()
//
template <typename T>
std::exception_ptr make_exception_ptr(const T& e)
{
    ut_assert_(!std::uncaught_exception() && "disallowed while an exception is propagating");

    std::exception_ptr eptr;

    try {
        throw e;
    } catch(...) {
        // MSVC10+ issue - std::current_exception returns empty here if
        //                 another exception is currently propagating
        eptr = std::current_exception();
    }

    return eptr;
}

// MSVC10 workaround -- no declval()
//
template <typename T> T&& declval();

}