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

#include "ExUtil.h"
#include <CppAwait/Log.h>
#include <array>
#include <string>
#include <boost/lexical_cast.hpp>

void ex_fibonacci();
void ex_iterator();
void ex_comboDetector();
void ex_awaitBasics();
void ex_awaitThread();
void ex_awaitHttpClient();
void ex_awaitFlickr();
void ex_awaitChatServer();
void ex_awaitChatClient();
void ex_stockServer();
void ex_stockClient();


struct Example
{
    void (*function)();
    const char *description;
};

static const Example EXAMPLES[] =
{
    { &ex_fibonacci, "coroutines - fibonacci sequence generator" },
    { &ex_iterator, "coroutines - collection iterator" },
    { &ex_comboDetector, "coroutines - combo detector" },
    { &ex_awaitBasics, "await - basics" },
    { &ex_awaitThread, "await - threads example" },
    { &ex_awaitHttpClient, "await - HTTP client" },
#ifdef HAVE_OPENSSL
    { &ex_awaitFlickr, "await - Flickr client" },
#endif
    { &ex_awaitChatServer, "await - chat server" },
    { &ex_awaitChatClient, "await - chat client" },
    { &ex_stockServer, "stock price server" },
    { &ex_stockClient, "stock price client" },
};

int main(int argc, char** argv)
{
    // ut::setLogLevel(ut::LOGLEVEL_DEBUG);

    size_t selected = 0;

    if (argc == 2) {
        selected = boost::lexical_cast<size_t>(argv[1]);
    }

    const int numExamples = sizeof(EXAMPLES) / sizeof(Example);

    while (selected < 1 || numExamples < selected) {
        printf ("Examples:\n\n");

        for (size_t i = 0; i < numExamples; i++) {
            printf ("%02d: %s\n", (int) (i + 1), EXAMPLES[i].description);
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
    example.function();

    printf ("\nDONE\n");

    return 0;
}
