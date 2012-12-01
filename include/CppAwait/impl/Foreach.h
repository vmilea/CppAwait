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

namespace ut {

// support for if/else trick

template<typename T>
struct false_wrapper
{
    false_wrapper(const T& value) : value(value) { }

    operator bool() const { return false; }

    T value;
};

template<typename T>
false_wrapper<T> make_false_wrapper(const T& value)
{
    return false_wrapper<T>(value);
}

template<typename T>
struct false_ref_wrapper
{
    false_ref_wrapper(T& value) : value(value) { }

    operator bool() const { return false; }

    T& value;

private:
    false_ref_wrapper& operator=(const false_ref_wrapper&);
};

template<typename T>
false_ref_wrapper<T> make_false_ref_wrapper(T& value)
{
    return false_ref_wrapper<T>(value);
}

template<typename T>
void increment(T& it)
{
    ++it;
}

}

//
// This macro is a workaround for missing range-based for in Visual Studio 2010.
//
// It it similar to BOOST_FOREACH but faster:
//   - optimized builds: as fast as a hand written loop
//   - debug builds: ~30% slower than a hand written loop
//
// Use notes:
// - rvalue VALS is not supported
// - begin()/end() evaluated only once
// - do not add/remove from the container while iterating!
//
// For the curious: http://www.artima.com/cppsource/foreach.html
//

#define ut_foreach_(VAL, VALS) \
    if (auto _foreach_col = ::ut::make_false_ref_wrapper(VALS)) { } else \
    if (auto _foreach_cur = ::ut::make_false_wrapper(std::begin(_foreach_col.value))) { } else \
    if (auto _foreach_end = ::ut::make_false_wrapper(std::end(_foreach_col.value))) { } else \
    for (bool _foreach_flag = true; \
              _foreach_flag && _foreach_cur.value != _foreach_end.value; \
              _foreach_flag ? ::ut::increment(_foreach_cur.value) : (void) 0) \
        if ((_foreach_flag = false) == true) { } else \
            for (VAL = *_foreach_cur.value; !_foreach_flag; _foreach_flag = true)
