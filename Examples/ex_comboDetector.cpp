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
#include <list>
#include <array>
#include <stdexcept>

//
// ABOUT: simulate FSM for detecting combos in a game
//

// valid combos
//
const std::array<std::string, 5> COMBOS =
{ {
    "xx",
    "yy",
    "xyx",
    "xyy",
    "yxx"
} };

// input sequence
//
const std::string BUTTON_MASH = "xxy-xyyx-yxx";

// checks for combo patterns in input sequence
//
// input  : char*   -- button pushed
// output : size_t* -- matched combo index, null if no match
//
static void coComboDetector(void *startValue)
{
    std::list<size_t> matches;
    
    auto resetMatches = [&]() {
        matches.clear();
        for (size_t i = 0; i < COMBOS.size(); i++) {
            matches.push_back(i);
        }
    };
    resetMatches();    

    size_t chrPos = 0;
    auto chr = (char *) startValue;

    while (chr != nullptr) {

        if (*chr == '-') { // start over
            resetMatches();
            chrPos = 0;
        } else if (*chr == 'x' || *chr == 'y') { // filter matches
            for (auto it = matches.begin(); it != matches.end() ; ) {
                const std::string& combo = COMBOS[*it];
            
                if (chrPos >= combo.size() || *chr != combo[chrPos]) {
                    it = matches.erase(it);
                } else {
                    ++it;
                }
            }
            chrPos++;
        } else {
            // ignore illegal input
        }

        if (matches.size() > 0 && COMBOS[matches.front()].size() == chrPos) {
            assert (matches.size() == 1);
            printf ("  @ %ld, match\n", (long) chrPos);

            chr = (char *) ut::yield(&matches.front());
        } else {
            printf ("  @ %ld, %ld possible matches\n", (long) chrPos, (long) matches.size());

            chr = (char *) ut::yield();
        }
    };
}

void ex_comboDetector()
{
    auto detectorCoro = new ut::Coro("combo-detector", &coComboDetector);

    foreach_(char chr, BUTTON_MASH) {
        auto match = (size_t *) ut::yieldTo(detectorCoro, &chr);

        if (match != nullptr) {
            printf ("matched '%s'\n", COMBOS[*match].c_str());
        }
    }

    ut::yieldTo(detectorCoro); // yield nullptr to quit
    delete detectorCoro;
}
