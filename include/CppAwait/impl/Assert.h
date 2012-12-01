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
#include <cassert>

#ifdef NDEBUG

#define ut_assert_(_condition) 
#define ut_assert_msg_(_condition, _format, ...) 

#else

#define ut_assert_(_condition) \
    UT_MULTI_LINE_MACRO_BEGIN \
    if (!(_condition)) { \
        fprintf(stderr, "CPP_ASYNC ASSERT FAILED: " #_condition "\n"); \
        assert (false); \
    } \
    UT_MULTI_LINE_MACRO_END

#define ut_assert_msg_(_condition, _format, ...) \
    UT_MULTI_LINE_MACRO_BEGIN \
    if (!(_condition)) { \
        fprintf(stderr, "CPP_ASYNC ASSERT FAILED: " #_condition  " --- " _format "\n", ##__VA_ARGS__); \
        assert (false); \
    } \
    UT_MULTI_LINE_MACRO_END

#endif
