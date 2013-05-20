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
#include <CppAwait/Awaitable.h>
#include <CppAwait/AsioWrappers.h>
#include <set>
#include <queue>

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
        MessageCRef msgRef = packMessage(sender, line);

        if (MAX_RECENT_MSGS > 0) {
            if (mRecentMsgs.size() == MAX_RECENT_MSGS) {
                mRecentMsgs.pop_front();
            }

            mRecentMsgs.push_back(msgRef);
        }

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
        , mSocket(std::make_shared<tcp::socket>(ut::asio::io())) { }

    // deleting the session will interrupt the coroutine
    ~ClientSession() { }

    const std::string& nickname() // override
    {
        return mNickname;
    }

    void deliver(MessageCRef msg) // override
    {
        mMsgQueue.push(msg);

        { ut::PushMasterCoro _; // take over
            // wake up writer
            mEvtMsgQueued();
        }
    }

    std::shared_ptr<tcp::socket> socket()
    {
        return mSocket;
    }

    ut::Awaitable& awaitable()
    {
        return mAwt;
    }

    void start()
    {
        auto recv = std::make_shared<boost::asio::streambuf>();

        // this coroutine reads inbound messages
        ut::Awaitable::AsyncFunc writer = [this](ut::Awaitable * /* awtSelf */) {
            do {
                if (mMsgQueue.empty()) {
                    ut::Awaitable awtMsgQueued("evt-msg-queued");
                    mEvtMsgQueued = awtMsgQueued.takeCompleter();

                    awtMsgQueued.await(); // yield until we have outbound messages
                } else {
                    MessageCRef msg = mMsgQueue.front();
                    mMsgQueue.pop();

                    ut::Awaitable awt = ut::asio::asyncWrite(*mSocket, msg);
                    awt.await(); // yield until message delivered
                }
            } while (true);
        };

        // this coroutine writes outbound messages
        ut::Awaitable::AsyncFunc reader = [this, recv](ut::Awaitable * /* awtSelf */) {
            std::string line;

            bool quit = false;
            do {
                ut::Awaitable awt = ut::asio::asyncReadUntil(*mSocket, recv, std::string("\n"));
                awt.await(); // yield until we have inbound messages

                std::istream recvStream(recv.get());
                std::getline(recvStream, line);

                if (line == "/leave") {
                    quit = true;
                } else {
                    mRoom.broadcast(mNickname, line);
                }
            } while (!quit);
        };

        // main coroutine handles handshake, reads & writes
        mAwt = ut::startAsync("clientSession-start", [this, writer, reader, recv](ut::Awaitable * /* awtSelf */) {
            // first message is nickname
            ut::Awaitable awt = ut::asio::asyncReadUntil(*mSocket, recv, std::string("\n"));
            awt.await();
            std::istream recvStream(recv.get());
            std::getline(recvStream, mNickname);

            mRoom.join(this);

            ut::Awaitable awtReader = ut::startAsync("clientSession-reader", reader);
            ut::Awaitable awtWriter = ut::startAsync("clientSession-writer", writer);

            // yield until /leave or Asio exception
            ut::Awaitable& done = ut::awaitAny(awtReader, awtWriter);

            mRoom.leave(this);

            done.await(); // check for exception, won't yield again since already done
        });
    }

private:
    ChatRoom& mRoom;

    std::shared_ptr<tcp::socket> mSocket;
    ut::Awaitable mAwt;
    std::string mNickname;

    std::queue<MessageCRef> mMsgQueue;
    ut::Completer mEvtMsgQueued;
};

// attribute shim for acessing the Awaitable of a ClientSession, used by ut::asyncAny()
inline ut::Awaitable* selectAwaitable(std::unique_ptr<ClientSession>& element)
{
    return &(element->awaitable());
}

static ut::Awaitable asyncChatServer(unsigned short port)
{
    // main coroutine manages client sessions
    return ut::startAsync("asyncChatServer", [port](ut::Awaitable * /* awtSelf */) {
        typedef std::list<std::unique_ptr<ClientSession> > SessionList;

        ChatRoom room;
        SessionList mSessions;

        tcp::endpoint endpoint(tcp::v4(), port);

        tcp::acceptor acceptor(ut::asio::io(), endpoint.protocol());
        try {
            acceptor.bind(endpoint);
            acceptor.listen();
        } catch (...) {
            printf ("Couldn't bind to port %u.\n", port);
            return;
        }

        std::unique_ptr<ClientSession> session;
        ut::Awaitable awtAccept;

        while (true) {
            printf ("waiting for clients to connect / disconnect...\n");

            if (!session) {
                // prepare for new connection
                session.reset(new ClientSession(room));
                awtAccept = ut::asio::asyncAccept(acceptor, session->socket());
            }

            SessionList::iterator posTerminated;

            // combine the list of awaitables for easier manipulation
            ut::Awaitable awtSessionTerminated = ut::asyncAny(mSessions, posTerminated);

            // yield until a new connection has been accepted / terminated
            ut::Awaitable& done = ut::awaitAny(awtAccept, awtSessionTerminated);

            if (&done == &awtAccept) {
                try {
                    awtAccept.await(); // check for exception
                    printf ("client acepted\n");

                    // start session coroutine
                    session->start();
                    mSessions.push_back(std::move(session));
                } catch(...) {
                    printf ("failed to accept client\n");
                }

                session = nullptr;
            } else {
                // remove terminated session
                ClientSession *session = posTerminated->get();

                try {
                    session->awaitable().await(); // check for exception
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
    ut::initScheduler(&asioSchedule);

    ut::Awaitable awt = asyncChatServer(3455);

    // loops until all async handlers have ben dispatched
    ut::asio::io().run();
}
