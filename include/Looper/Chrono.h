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

#include <CppAwait/Config.h>
#include <limits>

namespace loo {
    
const int64_t NSEC = 1LL;
const int64_t USEC = 1000LL;
const int64_t MSEC = 1000000LL;
const int64_t SEC = 1000000000LL;

struct Timepoint
{
    struct Diff
    {
        int64_t nanosSpan;

        Diff(const Timepoint& lhs, const Timepoint& rhs)
            : nanosSpan(lhs.nanos - rhs.nanos) { }

        int64_t nanoseconds()  const { return nanosSpan; }
        int64_t microseconds() const { return nanosSpan / USEC; }
        int32_t milliseconds() const { return (int32_t) (nanosSpan / MSEC); }
        int32_t seconds()      const { return (int32_t) (nanosSpan / SEC); }
    };

    int64_t nanos;

    explicit Timepoint(int64_t nanos)
        : nanos(nanos) { }

    Diff abs() const { return Diff(*this, Timepoint(0)); }

    Timepoint plusNano(int64_t nanoseconds)   const { return Timepoint(nanos + nanoseconds); }
    Timepoint plusMicro(int64_t microseconds) const { return Timepoint(nanos + microseconds * USEC); }
    Timepoint plusMilli(int64_t milliseconds) const { return Timepoint(nanos + milliseconds * MSEC); }
    Timepoint plusSeconds(int64_t seconds)    const { return Timepoint(nanos + seconds * SEC); }

    void addNano(int64_t nanoseconds)   { nanos += nanoseconds;          }
    void addMicro(int64_t microseconds) { nanos += microseconds * USEC; }
    void addMilli(int64_t milliseconds) { nanos += milliseconds * MSEC; }
    void addSeconds(int64_t seconds)    { nanos += seconds * SEC;       }

    Timepoint& operator+=(const Timepoint::Diff& diff)
    {
        nanos += diff.nanosSpan;
        return *this;
    }

    Timepoint& operator-=(const Timepoint::Diff& diff)
    {
        nanos -= diff.nanosSpan;
        return *this;
    }
};

static Timepoint TIMEPOINT_MAX(std::numeric_limits<int64_t>::max());

inline bool operator==(const Timepoint& lhs, const Timepoint& rhs)
{
    return lhs.nanos == rhs.nanos;
}

inline bool operator!=(const Timepoint& lhs, const Timepoint& rhs)
{
    return lhs.nanos != rhs.nanos;
}

inline int compare(const Timepoint& lhs, const Timepoint& rhs)
{
    if (lhs.nanos < rhs.nanos)
        return -1;
    if (lhs.nanos > rhs.nanos)
        return 1;
    return 0;
}

inline bool operator<(const Timepoint& lhs, const Timepoint& rhs)
{
    return compare(lhs, rhs) < 0;
}

inline bool operator<=(const Timepoint& lhs, const Timepoint& rhs)
{
    return compare(lhs, rhs) <= 0;
}

inline bool operator>(const Timepoint& lhs, const Timepoint& rhs)
{
    return compare(lhs, rhs) > 0;
}

inline bool operator>=(const Timepoint& lhs, const Timepoint& rhs)
{
    return compare(lhs, rhs) >= 0;
}

inline Timepoint::Diff operator-(const Timepoint& lhs, const Timepoint &rhs)
{
    return Timepoint::Diff(lhs, rhs);
}

inline Timepoint operator+(const Timepoint& lhs, const Timepoint::Diff& rhs)
{
    return lhs.plusNano(rhs.nanosSpan);
}

inline Timepoint operator+(const Timepoint::Diff& lhs, const Timepoint& rhs)
{
    return rhs.plusNano(lhs.nanosSpan);
}

inline Timepoint operator-(const Timepoint& lhs, const Timepoint::Diff& rhs)
{
    return lhs.plusNano(-rhs.nanosSpan);
}

//
// utility functions
//

void rebaseMonotonicTime();

Timepoint getMonotonicTime();

}
