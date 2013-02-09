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
#include <CppAwait/Awaitable.h>
#include <Looper/Looper.h>
#include <random>
#include <cmath>

// try to use std::thread, fallback to boost::thread
//
using namespace loo::lthread;
using namespace loo::lchrono;

//
// ABOUT: how to implement an Awaitable on top of threads,
//        how to handle interruption
//

static ut::AwaitableHandle asyncCountdown()
{
    return ut::startAsync("asyncCountdown", [](ut::Awaitable * /* awtSelf */) {
        ut::StackContext *context = ut::currentContext();
        
        timed_mutex mutex;
        condition_variable_any cond;
        bool isInterrupted = false;
        ut::Ticket completionTicket = 0;

        thread countdownThread([&]() {
            unique_lock<timed_mutex> lock(mutex);
            
            for (int i = 3; i > 0 && !isInterrupted; i--) {
                printf ("%d seconds until liftoff...\n", i);
                
                // up to 1 second of interruptible sleep
                auto timepoint = chrono::steady_clock::now() + chrono::milliseconds(1000);
                cond.wait_until(lock, timepoint, [&] { return isInterrupted; });
            }

            if (isInterrupted) {
                printf ("liftoff aborted!\n");
            } else {
                printf ("liftoff!\n");

                // MSVC10 workaround, inner lambda can't access captured variable
                timed_mutex& lambdaMutex = mutex;

                // resume awaitable, must do it from main thread
                completionTicket = ut::schedule([&]() {
                    { lock_guard<timed_mutex> _(lambdaMutex);
                        completionTicket = 0;
                    }
                    ut::yieldTo(context);
                });
            }
        });

        try {
            ut::yield();
        } catch (const ut::InterruptedException&) {
            // launch aborted, interrupt countdown thread
            lock_guard<timed_mutex> _(mutex);
            isInterrupted = true;
            cond.notify_one();
        }
        
        countdownThread.join();
        printf ("\njoined countdown thread\n");

        // unlikely case: interrupt was too late to prevent liftoff
        if (completionTicket != 0) {
            ut::cancelScheduled(completionTicket);
        }
    });
}

static ut::AwaitableHandle asyncKey()
{
    return ut::startAsync("asyncKey", [](ut::Awaitable * /* awtSelf */) {
        ut::StackContext *context = ut::currentContext();

        timed_mutex mutex;
        ut::Ticket completionTicket = 0;

        thread keyThread([&]() {
            lock_guard<timed_mutex> _(mutex);

            // Wait for user to hit [Return]. For illustration only. Relying on blocking
            // calls is bad practice, an awaitable should handle interruption quickly.
            readLine();

            // MSVC10 workaround, inner lambda can't access captured variable
            timed_mutex& lambdaMutex = mutex;

            completionTicket = ut::schedule([&]() {
                { lock_guard<timed_mutex> _(lambdaMutex);
                    completionTicket = 0;
                }
                ut::yieldTo(context);
            });
        });

        try {
            ut::yield();
        } catch (const ut::InterruptedException&) {
        }
        
        keyThread.join();
        printf ("\njoined key thread\n");

        if (completionTicket != 0) {
            ut::cancelScheduled(completionTicket);
        }
    });
}

static ut::AwaitableHandle asyncThread()
{
    return ut::startAsync("asyncThread", [](ut::Awaitable * /* awtSelf */) {
        printf ("hit [Return] to abort launch\n\n");
        
        {
            ut::AwaitableHandle awtCountdown = asyncCountdown();
            ut::AwaitableHandle awtKey = asyncKey();

            // wait until liftoff or abort
            ut::AwaitableHandle& completed = ut::awaitAny(awtCountdown, awtKey);

            if (completed == awtCountdown) {
                printf ("\nDONE. HIT RETURN TO QUIT...\n");
            } else {
                printf ("\nINTERRUPTED\n");
            }

            // scope end, the other awaitable will interrupt itself
        }

        ut::schedule([]() {
            loo::mainLooper().quit();
        });        
    });
}

void ex_awaitThread()
{
    ut::initMainContext();

    // use a custom run loop
    loo::Looper mainLooper("main");
    loo::setMainLooper(mainLooper);

    ut::AwaitableHandle awt = asyncThread();

    mainLooper.run();
}
