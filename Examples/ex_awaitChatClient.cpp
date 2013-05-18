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
#include "AsioScheduler.h"
#include <CppAwait/AsioWrappers.h>
#include "Looper/Thread.h"
#include <queue>

//
// ABOUT: chat client, similar to Asio chat example
//

using namespace boost::asio::ip;
using namespace loo::lthread;
using namespace loo::lchrono;

typedef std::shared_ptr<const std::string> MessageCRef;

// holds outbound messages
static std::queue<MessageCRef> sMsgQueue;

// used to notify when a new message has been queued
static std::unique_ptr<ut::Completable> sAwtMsgQueued;

// reads keyboard input and enqueues outbound messages
static void inputFunc()
{
    // sleep to tidy up output
    this_thread::sleep_for(chrono::milliseconds(50));
    printf (" > ");

    do {
        std::string line = readLine();

        // process the message on main loop
        ut::schedule([line]() {
            sMsgQueue.push(std::make_shared<std::string>(line + "\n"));

            // wake up writer
            if (!sAwtMsgQueued->didComplete()) {
                sAwtMsgQueued->complete();
            }
        });

        // sleep to tidy up output
        this_thread::sleep_for(chrono::milliseconds(50));
        printf (" > ");
    } while(true);
}

static ut::AwaitableHandle asyncChatClient(const std::string& host, const std::string& port, const std::string& nickname)
{
    // this coroutine reads & prints inbound messages
    auto reader = [](tcp::socket& socket, ut::Awaitable * /* awtSelf */) {
        auto recv = std::make_shared<boost::asio::streambuf>();
        std::string msg;

        do {
            ut::AwaitableHandle awt = ut::asio::asyncReadUntil(socket, recv, std::string("\n"));
            awt->await(); // yield until we have an inbound message

            std::istream recvStream(recv.get());
            std::getline(recvStream, msg);

            printf ("-- %s\n", msg.c_str());
        } while (true);
    };

    // this coroutine writes outbound messages
    auto writer = [](tcp::socket& socket, ut::Awaitable * /* awtSelf */) {
        bool quit = false;

        do {
            if (sMsgQueue.empty()) {
                sAwtMsgQueued->await(); // yield until we have outbound messages
                sAwtMsgQueued.reset(new ut::Completable("evt-msg-queued"));
            } else {
                MessageCRef msg = sMsgQueue.front();
                sMsgQueue.pop();

                ut::AwaitableHandle awt = ut::asio::asyncWrite(socket, msg);
                awt->await(); // yield until message delivered

                if (*msg == "/leave\n") {
                    quit = true;
                }
            }
        } while (!quit);
    };

    // main coroutine handles connection, handshake, reads & writes
    return ut::startAsync("asyncChatClient", [host, port, nickname, reader, writer](ut::Awaitable * /* awtSelf */) {
        try {
            ut::AwaitableHandle awt;

            tcp::socket socket(ut::asio::io());

            tcp::resolver resolver(ut::asio::io());
            tcp::resolver::query query(tcp::v4(), host, port);

            tcp::resolver::iterator itEndpoints;
            awt = ut::asio::asyncResolve(resolver, query, itEndpoints);
            awt->await();

            tcp::resolver::iterator itConnected;
            awt = ut::asio::asyncConnect(socket, itEndpoints, itConnected);
            awt->await();

            // Asio wrappers need some arguments passed as shared_ptr in order to support safe interruption
            auto msg = std::make_shared<std::string>(nickname.begin(), nickname.end());
            // all messages end with newline
            msg->push_back('\n');

            // first outbound message is always nickname
            awt = ut::asio::asyncWrite(socket, msg);
            awt->await();

            // read keyboard input on a different thread to keep main loop responsive
            thread inputThread(&inputFunc);
            inputThread.detach();
            sAwtMsgQueued.reset(new ut::Completable("evt-msg-queued"));

            // this coroutine reads & prints inbound messages
            ut::AwaitableHandle awtReader = ut::startAsync("chatClient-reader", [=, &socket](ut::Awaitable *awtSelf) {
                reader(socket, awtSelf);
            });

            // this coroutine writes outbound messages
            ut::AwaitableHandle awtWriter = ut::startAsync("chatClient-writer", [=, &socket](ut::Awaitable *awtSelf) {
                writer(socket, awtSelf);
            });

            // quit on /leave or Asio exception
            ut::AwaitableHandle& done = ut::awaitAny(awtReader, awtWriter);

            // cancel socket operations?
            // socket.shutdown(tcp::socket::shutdown_both);

            // trigger exception, if any
            done->await();
        } catch (const std::exception& ex) {
            printf ("Failed! %s - %s\n", typeid(ex).name(), ex.what());
        } catch (...) {
            printf ("Failed!\n");
        }
    });
}

void ex_awaitChatClient()
{
    printf ("your nickname: ");
    std::string nickname = readLine();

    // setup a scheduler on top of Boost.Asio io_service
    ut::initScheduler(&asioSchedule);

    ut::AwaitableHandle awt = asyncChatClient("localhost", "3455", nickname);

    // loops until all async handlers have ben dispatched
    ut::asio::io().run();
}
