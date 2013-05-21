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
#include <vector>
#include <string>
#include <cstdarg>

namespace ut {

//
// string utilities
//

#ifdef _MSC_VER

# define snprintf c99_snprintf
# define vsnprintf c99_vsnprintf

int c99_snprintf(char *outBuf, size_t size, const char *format, ...);
int c99_vsnprintf(char *outBuf, size_t size, const char *format, va_list ap);

#endif // _MSC_VER


int safe_printf(std::vector<char>& outBuf, size_t pos, const char *format, ...);
int safe_vprintf(std::vector<char>& outBuf, size_t pos, const char *format, va_list ap);

std::string string_printf(const char *format, ...);
std::string string_vprintf(const char *format, va_list ap);

}