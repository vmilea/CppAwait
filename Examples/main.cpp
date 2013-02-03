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
#include <CppAwait/impl/Log.h>
#include <array>
#include <string>
#include <boost/lexical_cast.hpp>

typedef void (*ExampleFunc)();

void ex_fibonacci();
void ex_iterator();
void ex_comboDetector();
void ex_awaitBasics();
void ex_awaitHttpClient();
void ex_awaitFlickr();
void ex_awaitThread();

const std::array<std::pair<ExampleFunc, std::string>, 7> EXAMPLES =
{ {
    std::make_pair(&ex_fibonacci, "coroutines - fibonacci sequence generator"),
    std::make_pair(&ex_iterator, "coroutines - collection iterator"),
    std::make_pair(&ex_comboDetector, "coroutines - combo detector"),
    std::make_pair(&ex_awaitBasics, "await - basics"),
    std::make_pair(&ex_awaitHttpClient, "await - HTTP client"),
    std::make_pair(&ex_awaitFlickr, "await - Flickr client"),
    std::make_pair(&ex_awaitThread, "await - threads example")
} };

int main(int argc, char** argv)
{
    // ut::setLogLevel(ut::LOGLEVEL_DEBUG);

    size_t selected = 0;

    if (argc == 2) {
        selected = boost::lexical_cast<size_t>(argv[1]);
    }

    while (selected < 1 || EXAMPLES.size() < selected) {
        printf ("Examples:\n\n");
            
        for (size_t i = 0; i < EXAMPLES.size(); i++) {
            printf ("%02d: %s\n", (int) (i + 1), EXAMPLES[i].second.c_str());
        }

        printf ("\nSelect: ");
        try {
            const char* line = readLine();
            selected = boost::lexical_cast<size_t>(line);
        } catch (...) {
        }

        printf ("\n---------\n\n");
    }

    auto& example = EXAMPLES[selected - 1];
    example.first();

    return 0;
}
