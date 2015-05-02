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

#pragma once

#include "../Config.h"
#include "Assert.h"
#include <exception>
#include <memory>

namespace ut {

// MSVC10 workaround -- no std::make_exception_ptr()
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

// MSVC10 workaround -- no conversion from std::exception_ptr to bool
//
inline bool is(const std::exception_ptr& eptr)
{
    return !(eptr == std::exception_ptr());
}

// MSVC10 workaround -- no std::declval()
//
template <typename T> T&& declval();

// make_unique missing in C++ 11
//

template<typename T>
std::unique_ptr<T> make_unique()
{
    return std::unique_ptr<T>(new T());
}

template<typename T, typename Arg1>
std::unique_ptr<T> make_unique(Arg1&& arg1)
{
    return std::unique_ptr<T>(new T(
        std::forward<Arg1>(arg1)));
}

template<typename T, typename Arg1, typename Arg2>
std::unique_ptr<T> make_unique(Arg1&& arg1, Arg2&& arg2)
{
    return std::unique_ptr<T>(new T(
        std::forward<Arg1>(arg1), std::forward<Arg2>(arg2)));
}

template<typename T, typename Arg1, typename Arg2, typename Arg3>
std::unique_ptr<T> make_unique(Arg1&& arg1, Arg2&& arg2, Arg3&& arg3)
{
    return std::unique_ptr<T>(new T(
        std::forward<Arg1>(arg1), std::forward<Arg2>(arg2), std::forward<Arg3>(arg3)));
}

template<typename T, typename Arg1, typename Arg2, typename Arg3, typename Arg4>
std::unique_ptr<T> make_unique(Arg1&& arg1, Arg2&& arg2, Arg3&& arg3, Arg4&& arg4)
{
    return std::unique_ptr<T>(new T(
        std::forward<Arg1>(arg1), std::forward<Arg2>(arg2), std::forward<Arg3>(arg3),
        std::forward<Arg4>(arg4)));
}

template<typename T, typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5>
std::unique_ptr<T> make_unique(Arg1&& arg1, Arg2&& arg2, Arg3&& arg3, Arg4&& arg4, Arg5&& arg5)
{
    return std::unique_ptr<T>(new T(
        std::forward<Arg1>(arg1), std::forward<Arg2>(arg2), std::forward<Arg3>(arg3),
        std::forward<Arg4>(arg4), std::forward<Arg5>(arg5)));
}

//
// misc
//

template<typename T>
std::unique_ptr<T> asUniquePtr(T&& value)
{
    return ut::make_unique<T>(std::move(value));
}

}