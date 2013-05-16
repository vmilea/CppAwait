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

//
// ABOUT: Simple client to check stock prices. Helps compare the 3 approaches:
//        1. blocking / 2. async await / 3. async callbacks

#include "ExUtil.h"
#include "AsioScheduler.h"
#include <CppAwait/AsioWrappers.h>
#include <map>
#include <functional>
#include <iostream>
#include <boost/lexical_cast.hpp>

using boost::asio::ip::tcp;

namespace ph = std::placeholders;

typedef std::map<std::string, float> StockMap;


// ----------------------------------------------
// blocking version
// ----------------------------------------------

static void fetchStocks_sync(const std::string& host, const std::string& port, StockMap& stocks)
{
    boost::asio::io_service io;

    try {
        tcp::resolver resolver(io);
        tcp::resolver::query query(tcp::v4(), host, port);

        tcp::resolver::iterator itEndpoints = resolver.resolve(query);

        tcp::socket socket(io);
        boost::asio::connect(socket, itEndpoints);

        boost::asio::streambuf requestBuf;
        boost::asio::streambuf replyBuf;

        foreach_(auto& stock, stocks) {
            const std::string& symbol = stock.first;
            float& price = stock.second;

            // write symbol
            std::ostream os(&requestBuf);
            os << symbol << "\n";
            boost::asio::write(socket, requestBuf);

            // read price
            boost::asio::read_until(socket, replyBuf, "\n");
            std::istream is(&replyBuf);
            is >> price;
            is.ignore(1, '\n'); // consume newline

            printf ("stock %s : %.2f\n", symbol.c_str(), price);
        }

        // end session
        boost::asio::write(socket, boost::asio::buffer("\n"));
    } catch (const std::exception& e) {
        fprintf (stderr, "Failed: %s - %s\n", typeid(e).name(), e.what());
    }
}


// ----------------------------------------------
// async await version
// ----------------------------------------------

static void fetchStocks_asyncAwait(const std::string& host, const std::string& port, StockMap& stocks)
{
    // setup a scheduler on top of Boost.Asio io_service
    ut::initScheduler(&asioSchedule);

    ut::AwaitableHandle awt = ut::startAsync("asyncFetchStocks", [&](ut::Awaitable * /* awtSelf */) {
        try {
            ut::AwaitableHandle awt;

            tcp::resolver resolver(ut::asio::io());
            tcp::resolver::query query(tcp::v4(), host, port);

            tcp::resolver::iterator itEndpoints;
            awt = ut::asio::asyncResolve(resolver, query, itEndpoints);
            awt->await();

            tcp::socket socket(ut::asio::io());

            tcp::resolver::iterator itConnected;
            awt = ut::asio::asyncConnect(socket, itEndpoints, itConnected);
            awt->await();

            auto requestBuf = std::make_shared<boost::asio::streambuf>();
            auto replyBuf = std::make_shared<boost::asio::streambuf>();

            foreach_(auto& stock, stocks) {
                const std::string& symbol = stock.first;
                float& price = stock.second;

                // write symbol
                std::ostream os(requestBuf.get());
                os << symbol << "\n";
                awt = ut::asio::asyncWrite(socket, requestBuf);
                awt->await();

                // read price
                awt = ut::asio::asyncReadUntil(socket, replyBuf, std::string("\n"));
                awt->await();
                std::istream is(replyBuf.get());
                is >> price;
                is.ignore(1, '\n'); // consume newline

                printf ("stock %s : %.2f\n", symbol.c_str(), price);
            }

            // end session
            awt = ut::asio::asyncWrite(socket, std::make_shared<std::string>("\n"));
            awt->await();
        } catch (const std::exception& e) {
            fprintf (stderr, "Failed: %s - %s\n", typeid(e).name(), e.what());
        }
    });

    // run main loop; loops until all async handlers have ben dispatched
    ut::asio::io().run();
}


// ----------------------------------------------
// async callbacks version
// ----------------------------------------------

class StockClient
{
public:
    StockClient(boost::asio::io_service& io, const std::string& host, const std::string& port, StockMap& stocks)
        : mStocks(stocks)
        , mResolverQuery(tcp::v4(), host, port)
        , mResolver(io)
        , mSocket(io)
    {
    }

    void start()
    {
        mResolver.async_resolve(mResolverQuery,
                                std::bind(&StockClient::handleResolved, this, ph::_1, ph::_2));
    }

private:
    void handleResolved(const boost::system::error_code& ec, tcp::resolver::iterator itEndpoints)
    {
        if (ec) {
            handleError(ec);
            return;
        }

        boost::asio::async_connect(mSocket, itEndpoints,
                                   std::bind(&StockClient::handleConnected, this, ph::_1, ph::_2));
    }

    void handleConnected(const boost::system::error_code& ec, tcp::resolver::iterator /* itConnected */)
    {
        if (ec) {
            handleError(ec);
            return;
        }

        mPos = mStocks.begin();
        doWriteSymbol();
    }

    void handleWroteSymbol(const boost::system::error_code& ec, std::size_t /* bytesTransferred */)
    {
        if (ec) {
            handleError(ec);
            return;
        }

        if (mPos == mStocks.end()) {
            handleDone();
            return;
        }

        doReadPrice();
    }

    void handleReadPrice(const boost::system::error_code& ec, std::size_t /* bytesTransferred */)
    {
        const std::string& symbol = mPos->first;
        float& price = mPos->second;

        std::istream is(&mReplyBuf);
        is >> price;
        is.ignore(1, '\n'); // consume newline

        printf ("<-- stock %s : %.2f\n", symbol.c_str(), price);

        mPos++;
        doWriteSymbol();

    }

    void handleError(const boost::system::error_code& ec)
    {
        fprintf (stderr, "Failed: %d - %s\n", ec.value(), ec.message().c_str());
    }

    void handleDone()
    {
    }

    void doWriteSymbol()
    {
        std::string symbol;

        if (mPos == mStocks.end()) {
            // send empty line to end session
        } else {
            symbol = mPos->first;
        }

        std::ostream os(&mRequestBuf);
        os << symbol << "\n";
        boost::asio::async_write(mSocket, mRequestBuf,
                                 std::bind(&StockClient::handleWroteSymbol, this, ph::_1, ph::_2));
    }

    void doReadPrice()
    {
        boost::asio::async_read_until(mSocket, mReplyBuf, "\n",
                                      std::bind(&StockClient::handleReadPrice, this, ph::_1, ph::_2));
    }

    StockMap& mStocks;
    tcp::resolver::query mResolverQuery;
    tcp::resolver mResolver;
    tcp::socket mSocket;

    StockMap::iterator mPos;
    boost::asio::streambuf mRequestBuf;
    boost::asio::streambuf mReplyBuf;
};

static void fetchStocks_asyncCallbacks(const std::string& host, const std::string& port, StockMap& stocks)
{
    boost::asio::io_service io;

    StockClient session(io, host, port, stocks);
    session.start();

    // run main loop; loops until all async handlers have ben dispatched
    io.run();
}

// ----------------------------------------------

void ex_stockClient()
{
    const std::string host = "localhost";
    const std::string port = "3455";

    // stocks to query for
    std::map<std::string, float> stocks;
    stocks["ARMH"] = 0;
    stocks["INTC"] = 0;
    stocks["TXN"] = 0;

    printf ("Select version:\n");
    printf ("1. blocking\n");
    printf ("2. async await\n");
    printf ("3. async callbacks\n");
    printf ("\n");
    printf ("> ");

    int selected = 0;
    try {
        const char* line = readLine();
        selected = boost::lexical_cast<size_t>(line);
    } catch (...) {
    }
    printf ("\n");

    if (selected == 1) {
        fetchStocks_sync(host, port, stocks);
    } else if (selected == 2) {
        fetchStocks_asyncAwait(host, port, stocks);
    } else if (selected == 3) {
        fetchStocks_asyncCallbacks(host, port, stocks);
    }
}
