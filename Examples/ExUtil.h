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

#include <cstdio>
#include <cstring>

#ifdef _MSC_VER

#ifdef NDEBUG
# define _SECURE_SCL 0
#endif

#ifndef _SCL_SECURE_NO_WARNINGS
# define _SCL_SECURE_NO_WARNINGS
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
# define _CRT_SECURE_NO_WARNINGS
#endif

#ifndef NOMINMAX
# define NOMINMAX
#endif

#endif // _MSC_VER


// borrow some helpers from library
//
#include <CppAwait/impl/Util.h>

// borrow foreach
//
#define foreach_ ut_foreach_


//
// extra
//

inline const char* readLine()
{
    static char line[512];
    
    void* result = fgets(line, sizeof(line), stdin);

    if (result == nullptr) {
        line[0] = '\0';
    } else {
        line[strlen(line) - 1] = '\0';
    }

    return line;
}
