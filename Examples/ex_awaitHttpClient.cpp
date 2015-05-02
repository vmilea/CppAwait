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
#include "Looper/Looper.h"
#include <CppAwait/Awaitable.h>
#include <CppAwait/AsioWrappers.h>
#include <fstream>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

//
// ABOUT: download an HTTP file using boost::asio wrappers
//

using namespace boost::asio::ip;


static boost::asio::io_service sIo;


static void doAsyncHttpGetHeader(tcp::socket& socket, const std::string& host, const std::string& path, std::shared_ptr<boost::asio::streambuf> outResponse, size_t& outContentLength)
{
    ut::Awaitable awt;

    tcp::resolver resolver(sIo);
    tcp::resolver::query query(host, "http");

    // DNS resolve
    tcp::resolver::iterator itEndpoints;
    awt = ut::asio::asyncResolve(resolver, query, itEndpoints);

    printf ("resolving %s ...\n", host.c_str());
    awt.await();

    // connect
    for (tcp::resolver::iterator it = itEndpoints, end = tcp::resolver::iterator(); it != end; ++it) {
        tcp::endpoint ep = *it;
        awt = ut::asio::asyncConnect(socket, ep);

        printf ("attempting connect to %s ...\n", ep.address().to_string().c_str());
        try {
            awt.await(); // connected!
            break;
        } catch (...) {
            // try next endpoint
        }
    }
    if (!socket.is_open()) {
        throw std::runtime_error("failed to connect socket");
    }

    printf ("connected!\n");

    auto request = std::make_shared<boost::asio::streambuf>();

    // write HTTP request
    std::ostream requestStream(request.get());
    requestStream << "GET " << path << " HTTP/1.1\r\n";
    requestStream << "Host: " << host << "\r\n";
    requestStream << "Accept: */*\r\n";
    requestStream << "Connection: close\r\n\r\n";

    awt = ut::asio::asyncWrite(socket, request);
    awt.await();

    // read first response line
    awt = ut::asio::asyncReadUntil(socket, outResponse, std::string("\r\n"));
    awt.await();

    std::istream responseStream(outResponse.get());
    std::string httpVersion;
    responseStream >> httpVersion;
    int statusCode;
    responseStream >> statusCode;
    std::string statusMessage;
    std::getline(responseStream, statusMessage);

    if (!responseStream || !boost::starts_with(httpVersion, "HTTP/")) {
        throw std::runtime_error("invalid response");
    }
    if (statusCode != 200) {
        throw std::runtime_error(ut::string_printf("bad status code: %d", statusCode));
    }

    // read response headers
    awt = ut::asio::asyncReadUntil(socket, outResponse, std::string("\r\n\r\n"));
    awt.await();

    // process headers
    std::string header;
    outContentLength = 0;

    printf ("headers:\n");

    while (std::getline(responseStream, header)) {
        if (boost::starts_with(header, "Content-Length: ")) {
            auto l = header.substr(strlen("Content-Length: "));
            l.resize(l.size() - 1);
            outContentLength = boost::lexical_cast<size_t>(l);
        }

        printf ("  %s\n", header.c_str());

        if (header == "\r") {
            break; // done reading header fields
        }
    }
}

static ut::Awaitable asyncHttpDownload(const std::string& host, const std::string& path, const std::string& savePath)
{
    return ut::startAsync("asyncHttpDownload", [host, path, savePath]() {
        tcp::socket socket(sIo);
        auto response = std::make_shared<boost::asio::streambuf>();
        size_t contentLength;
        size_t numBytesTransferred;

        try {
            // read header. it's fine to yield from inner function
            doAsyncHttpGetHeader(socket, host, path, response, contentLength);

            // transfer remaining content
            ut::Awaitable awt = ut::asio::asyncRead(socket, response,
                    boost::asio::transfer_exactly(contentLength - response->size()), numBytesTransferred);
            awt.await();

            printf("saving %ld bytes to file '%s' ...\n", (long) response->size(), savePath.c_str());

            // TODO: file I/O should also be async
            std::ofstream fout(savePath.c_str(), std::ios::out | std::ios::binary);
            fout << response.get();
        } catch (std::exception& e) {
            // exceptions get propagated into awaiting coroutine

            printf ("HTTP download failed: %s\n", e.what());
        }

        loo::mainLooper().schedule([] {
            loo::mainLooper().quit();
        });
    });
}

void ex_awaitHttpClient()
{
    // use a custom run loop
    loo::Looper mainLooper("main");
    loo::setMainLooper(mainLooper);

    // Dispatch Boost.Asio ready handlers every 5ms. This is a simple way to integrate
    // Asio into your GUI without hogging CPU. Having Asio drive the run loop instead
    // may give better performance (see Flickr example).
    //
    mainLooper.scheduleRepeating([]() -> bool {
        if (sIo.stopped()) {
            sIo.reset();
        }
        sIo.poll();

        return true;
    }, 0, 5);

    ut::Awaitable awt = asyncHttpDownload("www.google.com", "/images/srpr/logo3w.png", "download.png");

    mainLooper.run();
}
