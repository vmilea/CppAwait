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

#include "ConfigPrivate.h"
#include <CppAwait/AsioWrappers.h>
#include <CppAwait/impl/StringUtil.h>
#include <iostream>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

namespace ut { namespace asio {

using namespace boost::asio;
using namespace boost::asio::ip;

template <typename Socket>
static void doAsyncHttpGet(Socket& socket,
    const std::string& host, const std::string& path, bool persistentConnection,
    bool readAll, std::shared_ptr<streambuf> outResponse, size_t& outContentLength)
{
    if (!socket.lowest_layer().is_open()) {
        throw std::runtime_error("socket not connected");
    }

    auto request = std::make_shared<streambuf>();

    // write HTTP request
    std::ostream requestStream(request.get());
    requestStream << "GET " << path << " HTTP/1.1\r\n";
    requestStream << "Host: " << host << "\r\n";
    requestStream << "Accept: */*\r\n";
    if (!persistentConnection) {
        requestStream << "Connection: close\r\n";
    }
    requestStream << "\r\n";

    Awaitable awt = asyncWrite(socket, request);
    awt.await();

    // read first response line
    awt = asyncReadUntil(socket, outResponse, std::string("\r\n"));
    awt.await();

    std::istream responseStream(outResponse.get());
    std::string httpVersion;
    responseStream >> httpVersion;
    int statusCode;
    responseStream >> statusCode;
    std::string statusMessage;
    std::getline(responseStream, statusMessage);

    if (!responseStream || !boost::starts_with(httpVersion, "HTTP/")) {
        throw std::runtime_error("invalid HTTP response");
    }
    if (statusCode != 200) {
        throw std::runtime_error(string_printf("bad HTTP status: %d", statusCode));
    }

    // read response headers
    awt = asyncReadUntil(socket, outResponse, std::string("\r\n\r\n"));
    awt.await();

    // process headers
    std::string header;
    outContentLength = (size_t) -1;

    while (std::getline(responseStream, header) && header != "\r") {
        if (boost::starts_with(header, "Content-Length: ")) {
            auto l = header.substr(strlen("Content-Length: "));
            l.resize(l.size() - 1);
            outContentLength = boost::lexical_cast<size_t>(l);
        }
    }

    if (readAll) {
        size_t numBytesRemaining = outContentLength - outResponse->size();
        size_t numBytesTransferred;
        awt = asyncRead(socket, outResponse, asio::transfer_exactly(numBytesRemaining), numBytesTransferred);
        awt.await();
    }
}

namespace detail {

    void doAsyncHttpGet(boost::asio::ip::tcp::socket& socket,
        const std::string& host, const std::string& path, bool persistentConnection,
        bool readAll, std::shared_ptr<boost::asio::streambuf> outResponse, size_t& outContentLength)
    {
        ut::asio::doAsyncHttpGet(socket, host, path, persistentConnection, readAll, outResponse, outContentLength);
    }

#ifdef HAVE_OPENSSL
    void doAsyncHttpGet(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& socket,
        const std::string& host, const std::string& path, bool persistentConnection,
        bool readAll, std::shared_ptr<boost::asio::streambuf> outResponse, size_t& outContentLength)
    {
        ut::asio::doAsyncHttpGet(socket, host, path, persistentConnection, readAll, outResponse, outContentLength);
    }
#endif
}

Awaitable asyncHttpDownload(boost::asio::io_service& io,
    const std::string& host, const std::string& path,
    std::shared_ptr<streambuf> outResponse)
{
    static int id = 0;
    auto tag = string_printf("asyncHttpDownload-%d", id++);

    return startAsync(std::move(tag), [&io, host, path, outResponse]() {
        tcp::socket socket(io);

        tcp::resolver::query query(host, "http");
        tcp::resolver::iterator itConnected;
        Awaitable awt = asyncResolveAndConnect(socket, query, itConnected);
        awt.await();

        size_t contentLength;
        detail::doAsyncHttpGet(socket, host, path, false, true, outResponse, contentLength);
    });
}

#ifdef HAVE_OPENSSL

Awaitable asyncHttpsDownload(boost::asio::io_service& io,
    ssl::context_base::method sslVersion,
    const std::string& host, const std::string& path,
    std::shared_ptr<streambuf> outResponse)
{
    static int id = 0;
    auto tag = string_printf("asyncHttpsDownload-%d", id++);

    return startAsync(std::move(tag), [&io, sslVersion, host, path, outResponse]() {
        // prepare SSL client socket
        typedef ssl::stream<tcp::socket> ssl_socket;
        ssl::context ctx(sslVersion);
        ssl_socket socket(io, ctx);

        tcp::resolver::query query(host, "https");
        tcp::resolver::iterator itConnected;
        Awaitable awt = asyncResolveAndConnect(socket.lowest_layer(), query, itConnected);
        awt.await();

        socket.lowest_layer().set_option(tcp::no_delay(true));

        // perform SSL handshake
        awt = asyncHandshake(socket, ssl_socket::client);
        awt.await();

        size_t contentLength;
        detail::doAsyncHttpGet(socket, host, path, false, true, outResponse, contentLength);
    });
}

#endif // HAVE_OPENSSL

} }
