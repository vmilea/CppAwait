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

#include "ConfigPrivate.h"
#include <CppAwait/Log.h>
#include <CppAwait/impl/Assert.h>
#include <CppAwait/impl/StringUtil.h>
#include <cstring>
#include <cstdio>

namespace ut {

static const char* PREFIXES[] = { "", "[UT-WARN] ", "[UT-INFO] ", "[UT-DEBG] ", "[UT-VERB] " };

static const int PREFIX_LEN = 10;

static const int LOG_BUF_SIZE = 1024;

static char sBuffer[LOG_BUF_SIZE];


LogLevel gLogLevel = LOGLEVEL_WARN;


void implLog(LogLevel logLevel, const char *format, ...)
{
    ut_assert_(logLevel <= gLogLevel);

    va_list ap;
    va_start(ap, format);

    memcpy(sBuffer, PREFIXES[logLevel], PREFIX_LEN + 1);

    vsnprintf(sBuffer + PREFIX_LEN, LOG_BUF_SIZE - PREFIX_LEN, format, ap);

    printf("%s\n", sBuffer);

    va_end(ap);
}

}
