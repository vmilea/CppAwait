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

#include "ExUtil.h"
#include <CppAwait/Coro.h>
#include <CppAwait/YieldSequence.h>
#include <array>

//
// ABOUT: generator - iterates over a filtered collection
//

template <typename C, typename P>
static void filteredIterator(const C& collection, P predicate)
{
    foreach_(auto& value, collection) {
        if (predicate(value)) {
            ut::yield((void*) &value);
        }
    }

    // simply return to finish iteration
}

void ex_iterator()
{
    std::array<int, 10> digits = { { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 } };

    auto coOddDigits = [&](void*) {
        filteredIterator(digits, [](int value) { return value % 2 == 1; });
    };

    ut::YieldSequence<int> oddDigits(coOddDigits);

    foreach_(int value, oddDigits) {
        printf ("%d\n", value);
    }
}
