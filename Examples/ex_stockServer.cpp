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

//
// ABOUT: Fake stock server. Uses blocking API to keep it simple, can
//        only deal with one client at a time. For an asynchronous server
//        see ex_awaitChatServer.cpp.
//

#include <cstdio>
#include <string>
#include <map>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>

using boost::asio::ip::tcp;

typedef std::map<std::string, float> StockMap;

void ex_stockServer()
{
    // dummy stock prices
    std::map<std::string, float> stocks;
    stocks["INTC"] = 22.39;
    stocks["AMD"] = 2.61;
    stocks["NVDA"] = 12.70;
    stocks["ARMH"] = 40.70;
    stocks["TXN"] = 35.75;

    boost::asio::io_service io;

    tcp::endpoint endpoint(tcp::v4(), 3455);

    tcp::acceptor acceptor(io, endpoint.protocol());
    try {
        acceptor.bind(endpoint);
        acceptor.listen();
    } catch (...) {
        fprintf (stderr, "Couldn't bind to port %u!\n", endpoint.port());
        return;
    }

    while (true) {
        printf ("waiting for new client...\n");

        tcp::socket socket(io);

        try {
            acceptor.accept(socket);

            bool quit = false;

            boost::asio::streambuf requestBuf;
            boost::asio::streambuf replyBuf;

            // for each stock symbol read, reply with stock price. Client
            // should send an empty line before disconnecting.
            //
            do {
                std::string symbol;

                boost::asio::read_until(socket, requestBuf, "\n");
                std::istream is(&requestBuf);
                is >> symbol;
                is.ignore(1, '\n'); // consume newline

                if (symbol.size() >= 3) {
                    float price = 0.0f;
                    StockMap::iterator pos = stocks.find(symbol);
                    if (pos != stocks.end()) {
                        price = pos->second;
                    }

                    printf ("--> stock %s : %.2f\n", symbol.c_str(), price);

                    std::ostream os(&replyBuf);
                    os << price << "\n";
                    boost::asio::write(socket, replyBuf);
                } else {
                    printf ("client session finished\n");
                    quit = true;
                }
            } while (!quit);
        } catch (const std::exception& e) {
            fprintf (stderr, "Session error: %s - %s\n", typeid(e).name(), e.what());
        }
    }
}


