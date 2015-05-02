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

#include "Config.h"
#include <cstdarg>

namespace ut {

enum LogLevel
{
    LOGLEVEL_NONE,
    LOGLEVEL_WARN,
    LOGLEVEL_INFO,
    LOGLEVEL_DEBUG,
    LOGLEVEL_VERBOSE
};

extern LogLevel gLogLevel;

inline void setLogLevel(LogLevel logLevel)
{
    gLogLevel = logLevel;
}

inline LogLevel logLevel()
{
    return gLogLevel;
}

void implLog(LogLevel logLevel, const char *format, ...);

}

#ifdef UT_DISABLE_LOGGING

#define ut_log_warn_(    _format, ...)
#define ut_log_info_(    _format, ...)
#define ut_log_debug_(   _format, ...)
#define ut_log_verbose_( _format, ...)

#else  // not UT_DISABLE_LOGGING

// lazy argument evaluation
//
#define ut_impl_log_(_level, _format, ...) \
    ut_multi_line_macro_begin_ \
    if (_level <= ut::gLogLevel) { \
        ut::implLog(_level, _format, ##__VA_ARGS__); \
    } \
    ut_multi_line_macro_end_

#define ut_log_warn_(    _format, ...) ut_impl_log_(ut::LOGLEVEL_WARN,    _format, ##__VA_ARGS__)
#define ut_log_info_(    _format, ...) ut_impl_log_(ut::LOGLEVEL_INFO,    _format, ##__VA_ARGS__)
#define ut_log_debug_(   _format, ...) ut_impl_log_(ut::LOGLEVEL_DEBUG,   _format, ##__VA_ARGS__)
#define ut_log_verbose_( _format, ...) ut_impl_log_(ut::LOGLEVEL_VERBOSE, _format, ##__VA_ARGS__)

#endif // UT_DISABLE_LOGGING
