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

void setLogLevel(LogLevel logLevel);
LogLevel logLevel();

void implLog(LogLevel logLevel, const char *format, ...);

}

#ifndef UT_LOG_WHERE_FORMAT
# define UT_LOG_WHERE_FORMAT "(%s - %d) "
#endif

#ifndef UT_LOG_WHERE_ARGS
# define UT_LOG_WHERE_ARGS __FUNCTION__, __LINE__
#endif

// #define ut_log_error_(_format, ...) ut::implLog(ut::LOGLEVEL_ERROR, UT_LOG_WHERE_FORMAT _format, UT_LOG_WHERE_ARGS, __VA_ARGS__)

#define ut_log_warn_(_format, ...) ut::implLog(ut::LOGLEVEL_WARN, _format, ##__VA_ARGS__)

#define ut_log_info_(_format, ...) ut::implLog(ut::LOGLEVEL_INFO, _format, ##__VA_ARGS__)

#define ut_log_debug_(_format, ...) ut::implLog(ut::LOGLEVEL_DEBUG, _format, ##__VA_ARGS__)

#define ut_log_verbose_(_format, ...) ut::implLog(ut::LOGLEVEL_VERBOSE, _format, ##__VA_ARGS__)
