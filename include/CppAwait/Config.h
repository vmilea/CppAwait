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

#include <stddef.h>

#ifndef WINVER
#define WINVER 0x501
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT WINVER
#endif

///

#ifdef _MSC_VER

#define ut_concatenate_(s1, s2)          s1##s2
#define ut_concatenate_indirect_(s1, s2) ut_concatenate_(s1, s2)
#define ut_anonymous_variable_(str)      ut_concatenate_indirect_(str, __LINE__)

//

#define ut_multi_line_macro_begin_ \
    do { \
    __pragma(warning(push)) \
    __pragma(warning(disable:4127))

#define ut_multi_line_macro_end_ \
    } while(0) \
    __pragma(warning(pop))

#else // not _MSC_VER

#define ut_multi_line_macro_begin_ do {
#define ut_multi_line_macro_end_ } while(0)

#endif // _MSC_VER

///

#ifndef va_copy
# ifdef __va_copy
#  define va_copy(a,b) __va_copy(a,b)
# else
#  define va_copy(a, b) memcpy(&(a), &(b), sizeof(a))
# endif
#endif

//

#ifdef __cplusplus

#include <boost/cstdint.hpp>
#include <exception>

namespace ut {

// VC++ 2010 workaround -- no make_exception_ptr()
//
template <typename T>
std::exception_ptr make_exception_ptr(const T& e)
{
    try {
        throw e;
    } catch(...) {
        return std::current_exception();
    }
}

// VC++ 2010 workaround -- no declval()
//
template <typename T> T&& declval();

}

#endif // __cplusplus
