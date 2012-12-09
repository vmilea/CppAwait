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
#include <array>

//
// ABOUT: how to define, use & combine Awaitables
//

// simple awaitable, without a stack context
//
static ut::AwaitableHandle asyncMySimpleDelay(long delay)
{
    // on calling context
    ut::Completable *awt = new ut::Completable();
    
    ut::Ticket ticket = ut::scheduleDelayed(delay, [=]() {
        // on 'main' context
        awt->complete();
    });

    // cancel ticket if interrupted
    awt->setOnDoneHandler([ticket](ut::Awaitable *awt) {
        if (awt->didFail()) {
            ut::cancelScheduled(ticket);
        }
    });

    // awaitables can be tagged to ease debugging
    awt->setTag("my-simple-delay");

    return ut::AwaitableHandle(awt);
}

// Awaitable with dedicated context. Having a separate context means you can
// yield/await. await() *does not block* , instead it yields to main context.
// After task is done, awaiting context will resume.
//
static ut::AwaitableHandle asyncMyDelay(long delay)
{
    // on calling context
    std::string tag = ut::string_printf("my-delay-%ld", delay);

    return ut::startAsync(tag, [=](ut::Awaitable *self) {
        // on 'my-delay-2' context
        printf ("'%s' - start\n", ut::currentContext()->tag());

        ut::AwaitableHandle awt = asyncMySimpleDelay(delay);

        // free to do other stuff...

        awt->await(); // yield until awt done

        printf ("'%s' - done\n", ut::currentContext()->tag());
    });
}

// test coroutine
//
static ut::AwaitableHandle asyncTest()
{
    return ut::startAsync("test", [](ut::Awaitable *self) {
        // on 'test' context
        printf ("'%s' - start\n", ut::currentContext()->tag());

        // it's trivial to compose awaitables
        std::array<ut::AwaitableHandle, 3> awts = { {
            asyncMyDelay(400),
            asyncMyDelay(300),
            asyncMyDelay(800)
        } };
        ut::awaitAll(awts);
        
        printf ("'%s' - done\n", ut::currentContext()->tag());

        ut::schedule([]() {
            loo::mainLooper().quit();
        });

        // AwaitableHandle is a unique_ptr<Awaitable>. When awaitable gets
        // destroyed it releases bound context (if any).
    });
}

void ex_awaitBasics()
{
    // initialize on main stack
    ut::initMainContext();

    // Your application needs a run loop, otherwise there's no way to awake from await.
    // CppAwait has three hooks -- schedule()/scheduleDelayed()/cancelScheduled() -- that
    // should be implemented using your API of choice (Qt/GLib/MFC/...).
    // 
    // here we just use a custom run loop
    loo::Looper mainLooper("main");
    loo::setMainLooper(mainLooper);

    // print every 100ms to show main loop is not blocked
    mainLooper.scheduleRepeating([]() -> bool {
        printf ("...\n"); // on main context
        return true;
    }, 0, 100);

    ut::AwaitableHandle awt = asyncTest();
    
    printf ("'%s' - START\n", ut::currentContext()->tag());

    mainLooper.run();

    printf ("'%s' - END\n", ut::currentContext()->tag());
}
