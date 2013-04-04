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
#include "AsioScheduler.h"
#include <CppAwait/Awaitable.h>
#include <CppAwait/AsioWrappers.h>
#include <set>
#include <queue>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

//
// ABOUT: chat server, similar to Asio chat example
//

using namespace boost::asio;
using namespace boost::asio::ip;

typedef std::shared_ptr<const std::string> MessageCRef;

inline MessageCRef packMessage(const std::string& sender, const std::string& line)
{
    // chat messages are delimited by newline
    return std::make_shared<std::string>(
        ut::string_printf("%s: %s\n", sender.c_str(), line.c_str()));
}

// chat guest interface
class Guest
{
public:
    virtual ~Guest() { }

    virtual const std::string& nickname() = 0;

    virtual void deliver(MessageCRef msg) = 0;
};

// chat room manages guests and message delivery
class ChatRoom
{
public:
    void join(Guest *guest)
    {
        mGuests.insert(guest);

        // deliver recent history
        ut_foreach_(auto& msg, mRecentMsgs) {
            guest->deliver(msg);
        }

        std::string line = ut::string_printf("%s has joined", guest->nickname().c_str());
        printf ("%s\n", line.c_str());

        // notify all guests
        broadcast(":server", line);
    }

    void leave(Guest *guest)
    {
        auto pos = mGuests.find(guest);

        if (pos != mGuests.end()) {
            mGuests.erase(pos);

            std::string line = ut::string_printf("%s has left", guest->nickname().c_str());
            printf ("%s\n", line.c_str());

            // notify all clients
            broadcast(":server", line);
        }
    }

    void broadcast(const std::string& sender, const std::string& line)
    {
        if (mRecentMsgs.size() == MAX_RECENT_MSGS) {
            mRecentMsgs.pop_front();
        }

        MessageCRef msgRef = packMessage(sender, line);
        mRecentMsgs.push_back(msgRef);

        ut_foreach_(Guest *guest, mGuests) {
            guest->deliver(msgRef);
        }
    }

private:
    static const size_t MAX_RECENT_MSGS = 10;

    std::set<Guest *> mGuests;
    std::deque<MessageCRef> mRecentMsgs;
};

// manages client coroutine
class ClientSession : public Guest
{
public:
    ClientSession(ChatRoom& room)
        : mRoom(room)
        , mSocket(std::make_shared<tcp::socket>(ut::asio::io()))
    {
        mAwtMsgQueued.reset(new ut::Completable());
    }

    // deleting the session will interrupt the coroutine
    ~ClientSession()
    {
        mRoom.leave(this);
    }

    const std::string& nickname() // override
    {
        return mNickname;
    }

    void deliver(MessageCRef msg) // override
    {
        mMsgQueue.push(msg);

        // wake up writer. scheduleComplete() must be used because we're not on main context
        mAwtMsgQueued->scheduleComplete();
    }

    std::shared_ptr<tcp::socket> socket()
    {
        return mSocket;
    }

    ut::Awaitable* awaitable()
    {
        return mAwt.get();
    }

    void start()
    {
        // session coroutine handles handshake, reads & writes
        mAwt = ut::startAsync("clientSession-start", [this](ut::Awaitable * /* awtSelf */) {
            auto recv = std::make_shared<boost::asio::streambuf>();

            // first message is nickname
            ut::AwaitableHandle awt = ut::asio::asyncReadUntil(*mSocket, recv, std::string("\n"));
            awt->await();
            std::istream recvStream(recv.get());
            std::getline(recvStream, mNickname);

            mRoom.join(this);

            // this coroutine reads inbound messages
            ut::AwaitableHandle awtReader = ut::startAsync("clientSession-reader", [this](ut::Awaitable * /* awtSelf */) {
                auto recv = std::make_shared<boost::asio::streambuf>();
                std::string line;

                bool quit = false;
                do {
                    ut::AwaitableHandle awt = ut::asio::asyncReadUntil(*mSocket, recv, std::string("\n"));
                    awt->await(); // yield until we have inbound messages

                    std::istream recvStream(recv.get());
                    std::getline(recvStream, line);

                    if (line == "/leave") {
                        quit = true;
                    } else {
                        mRoom.broadcast(mNickname, line);
                    }
                } while (!quit);
            });

            // this coroutine writes outbound messages
            ut::AwaitableHandle awtWriter = ut::startAsync("clientSession-writer", [this](ut::Awaitable * /* awtSelf */) {
                do {
                    if (mMsgQueue.empty()) {
                        mAwtMsgQueued->await(); // yield until we have outbound messages
                        mAwtMsgQueued.reset(new ut::Completable());
                    } else {
                        MessageCRef msg = mMsgQueue.front();
                        mMsgQueue.pop();

                        ut::AwaitableHandle awt = ut::asio::asyncWrite(*mSocket, msg);
                        awt->await(); // yield until message delivered
                    }
                } while (true);
            });

            ut::AwaitableHandle& done = ut::awaitAny(awtReader, awtWriter);
            done->await(); // check for exception
        });
    }

private:
    ChatRoom& mRoom;

    std::shared_ptr<tcp::socket> mSocket;
    ut::AwaitableHandle mAwt;
    std::string mNickname;

    std::queue<MessageCRef> mMsgQueue;
    std::unique_ptr<ut::Completable> mAwtMsgQueued;
};

// attribute shim for acessing the Awaitable of a ClientSession, used by ut::asyncAny()
inline ut::Awaitable* selectAwaitable(std::unique_ptr<ClientSession>& element)
{
    return element->awaitable();
}

static ut::AwaitableHandle asyncChatServer(unsigned short port)
{
    // main coroutine manages client sessions
    return ut::startAsync("asyncChatServer", [port](ut::Awaitable * /* awtSelf */) {
        typedef std::list<std::unique_ptr<ClientSession> > SessionList;

        ChatRoom room;
        SessionList mSessions;

        tcp::endpoint endpoint(tcp::v4(), port);
        tcp::acceptor acceptor(ut::asio::io(), endpoint);

        while (true) {
            printf ("waiting for clients to connect / disconnect...\n");

            std::unique_ptr<ClientSession> session(new ClientSession(room));
            SessionList::iterator posTerminated;

            ut::AwaitableHandle awtAccept = ut::asio::asyncAccept(acceptor, session->socket());

            // combine the list of awaitables for easier manipulation
            ut::AwaitableHandle awtSessionTerminated = ut::asyncAny(mSessions, posTerminated);

            ut::AwaitableHandle& done = ut::awaitAny(awtAccept, awtSessionTerminated);

            if (done == awtAccept) {
                try {
                    awtAccept->await(); // check for exceptions
                    printf ("client acepted\n");
                } catch(...) {
                    printf ("failed to accept client\n");
                }

                // start session coroutine
                session->start();
                mSessions.push_back(std::move(session));
            } else {
                // remove terminated session
                ClientSession *session = posTerminated->get();

                try {
                    session->awaitable()->await(); // check for exceptions
                    printf ("client '%s' has left\n", session->nickname().c_str());
                } catch(...) {
                    printf ("client '%s' disconnected\n", session->nickname().c_str());
                }

                mSessions.erase(posTerminated);
            }
        }
    });
}

void ex_awaitChatServer()
{
    // setup a scheduler on top of Boost.Asio io_service
    ut::initScheduler(&asioScheduleDelayed, &asioCancelScheduled);

    ut::AwaitableHandle awt = asyncChatServer(3455);

    // blocks until all async handlers have ben dispatched
    ut::asio::io().run();
}
