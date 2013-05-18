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

#include "ExUtil.h"
#include "LooScheduler.h"
#include "AsioScheduler.h"
#include <CppAwait/Awaitable.h>
#include <CppAwait/AsioWrappers.h>
#include <Looper/Looper.h>
#include <array>

//
// ABOUT: how to define, use & combine Awaitables
//

// simple awaitable without coroutine
//
static ut::AwaitableHandle asyncSimpleDelay(long delay)
{
    // on calling coroutine

    ut::Completable *awt = new ut::Completable();

    // awaitables can be tagged to ease debugging
    awt->setTag(ut::string_printf("simple-delay-%ld", delay));

    // Schedule completion after delay milliseconds. Exactly what triggers
    // completion is an implementation detail -- here we use an Asio
    // deadline_timer. The only thing that matters is to call complete()
    // from master coroutine (i.e. your main loop).

    auto timer = new boost::asio::deadline_timer(
        ut::asio::io(), boost::posix_time::milliseconds(delay));

    timer->async_wait([awt](const boost::system::error_code& ec) {
        // on master coroutine (io_service)

        if (ec == boost::asio::error::operation_aborted) {
            return; // awt is already destroyed
        }
        awt->complete(); // yields to awaiting coroutine
    });

    // cancel timer if interrupted
    awt->connectToDone([timer](ut::Awaitable *awt) {
        delete timer; // posts error::operation_aborted
    });

    return ut::AwaitableHandle(awt);
}

// Awaitable with dedicated coroutine. While in a coroutine you may yield.
// await() simply means 'yield until task is done'. It does not block, instead
// it yields to main loop if necessary until task done.
//
static ut::AwaitableHandle asyncCoroDelay(long delay)
{
    // on calling coroutine
    std::string tag = ut::string_printf("coro-delay-%ld", delay);

    return ut::startAsync(tag, [=](ut::Awaitable *self) {
        // on 'coro-delay' coroutine
        printf ("'%s' - start\n", ut::currentCoro()->tag());

        ut::AwaitableHandle awt = asyncSimpleDelay(delay);

        // free to do other stuff...

        awt->await(); // yield until awt done

        printf ("'%s' - done\n", ut::currentCoro()->tag());
    });
}

// test coroutine
//
static ut::AwaitableHandle asyncTest()
{
    return ut::startAsync("test", [](ut::Awaitable *self) {
        // on 'test' coroutine
        printf ("'%s' - start\n", ut::currentCoro()->tag());

        // it's trivial to compose awaitables
        std::array<ut::AwaitableHandle, 3> awts = { {
            asyncSimpleDelay(400),
            asyncCoroDelay(300),
            asyncCoroDelay(800)
        } };
        ut::awaitAll(awts);
        
        printf ("'%s' - done\n", ut::currentCoro()->tag());

        ut::asio::io().stop();

        // AwaitableHandle is a unique_ptr<Awaitable>. When awaitable gets
        // destroyed it releases bound coroutine (if any).
    });
}

void ex_awaitBasics()
{
    // Your application needs a run loop to alternate between Awaitables.
    // CppAwait relies on a schedule() hook that must be implemented using your API
    // of choice (Qt / GLib / MFC / Asio ...).
    //
    // here we run on top of Boost.Asio io_service
    ut::initScheduler(&asioSchedule);

    ut::AwaitableHandle awtTest = asyncTest();

    // print every 100ms to show main loop is not blocked
    ut::AwaitableHandle awtTicker = ut::startAsync("ticker", [](ut::Awaitable *self) {
        while (true) {
            ut::AwaitableHandle awt = asyncSimpleDelay(100);
            awt->await();

            printf ("...\n");
        }
    });

    // Awaitables started from main stack are fire-and-forget, can't await()

    printf ("'%s' - START\n", ut::currentCoro()->tag());

    ut::asio::io().run();

    printf ("'%s' - END\n", ut::currentCoro()->tag());
}
