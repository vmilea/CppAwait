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

#include "ConfigPrivate.h"
#include <CppAwait/impl/StringUtil.h>
#include <CppAwait/impl/Assert.h>

namespace ut {

#ifdef _MSC_VER

int c99_snprintf(char *outBuf, size_t size, const char *format, ...)
{
    int count;
    va_list ap;

    va_start(ap, format);
    count = c99_vsnprintf(outBuf, size, format, ap);
    va_end(ap);

    return count;
}

int c99_vsnprintf(char *outBuf, size_t size, const char *format, va_list ap)
{
    int count = -1;

    if (size != 0)
        count = _vsnprintf_s(outBuf, size, _TRUNCATE, format, ap);
    if (count == -1)
        count = _vscprintf(format, ap);

    return count;
}

#endif

int safe_vprintf(std::vector<char>& outBuf, size_t pos, const char *format, va_list ap)
{
    ut_assert_(pos <= outBuf.size());

    va_list apCopy;
    va_copy(apCopy, ap);

    int numChars = vsnprintf(outBuf.data() + pos, outBuf.size() - pos, format, ap);

    if (numChars >= 0) {
        if (numChars >= (int) (outBuf.size() - pos)) {
            size_t newSize = pos + numChars + 1;

            if (newSize < outBuf.size() * 2 + 64)
                newSize = outBuf.size() * 2 + 64;

            outBuf.resize(newSize);
            vsnprintf(outBuf.data() + pos, outBuf.size() - pos, format, apCopy);
        }
    }

    return numChars;
}

int safe_printf(std::vector<char>& outBuf, size_t pos, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int numChars = safe_vprintf(outBuf, pos, format, ap);
    va_end(ap);
    return numChars;
}

std::string string_vprintf(const char *format, va_list ap)
{
    std::vector<char> buf;
    int numChars = safe_vprintf(buf, 0, format, ap);

    return (numChars < 0 ? "" : std::string(buf.data(), numChars));
}

std::string string_printf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    std::string s = string_vprintf(format, ap);
    va_end(ap);
    return s;
} 

}