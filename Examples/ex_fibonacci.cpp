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
// ABOUT: generator - fibonacci sequence
//

static void coFiboGenerator(void *startValue)
{
    (void) startValue; // unused

    long a = 0, b = 1;
    ut::yield(&a);
    ut::yield(&b);

    do {
        long temp = a;
        a = b;
        b += temp;

        ut::yield(&b);
    } while (true);
}

void ex_fibonacci()
{
    {
        auto fiboCoro = new ut::Coro("fibo-generator", &coFiboGenerator);

        for (int i = 0; i < 10; i++) {
            // yield nullptr to coroutine
            auto value = (long *) ut::yieldTo(fiboCoro);

            // back from coroutine. value points to an integer on fibo stack
            printf ("%ld\n", *value);
        }
        printf ("\n\n");

        // Terminate coroutine via exception. You could also yield a flag
        // that coroutine checks to see if it should quit.
        ut::forceUnwind(fiboCoro);

        delete fiboCoro;
    }

    {
        // YieldSequence makes it easier to iterate over generators

        ut::YieldSequence<long> fibos(&coFiboGenerator);

        int i = 0;
        foreach_(long value, fibos) {
            printf ("%ld\n", value);

            if (++i == 10)
                break;
        }
    }
}
