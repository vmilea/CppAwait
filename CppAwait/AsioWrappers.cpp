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
#include <CppAwait/impl/Util.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

namespace ut { namespace asio {

using namespace boost::asio;
using namespace boost::asio::ip;

void doAsyncHttpGetHeader(tcp::socket& socket, const std::string& host, const std::string& path, std::shared_ptr<streambuf> outResponse, size_t& outContentLength)
{
    AwaitableHandle awt;
        
    tcp::resolver resolver(io());
    tcp::resolver::query query(host, "http");
    
    // DNS resolve
    tcp::resolver::iterator itEndpoints;
    awt = asyncResolve(resolver, query, itEndpoints);
    awt->await();

    // connect
    tcp::resolver::iterator itConnected;
    awt = asyncConnect(socket, itEndpoints, itConnected);
    awt->await();

    if (!socket.is_open()) {
        throw std::runtime_error("failed to connect socket");
    }

    auto request = std::make_shared<streambuf>();

    // write HTTP request
    std::ostream requestStream(request.get());
    requestStream << "GET " << path << " HTTP/1.0\r\n";
    requestStream << "Host: " << host << "\r\n";
    requestStream << "Accept: */*\r\n";
    requestStream << "Connection: close\r\n\r\n";

    awt = asyncWrite(socket, request);
    awt->await();

    // read first response line
    awt = asyncReadUntil(socket, outResponse, std::string("\r\n"));
    awt->await();

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
        throw std::runtime_error(string_printf("bad status code: %d", statusCode));
    }

    // read response headers
    awt = asyncReadUntil(socket, outResponse, std::string("\r\n\r\n"));
    awt->await();

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
}

AwaitableHandle asyncHttpDownload(const std::string& host, const std::string& path, std::shared_ptr<streambuf> outResponse)
{
    static int id = 0;
    auto tag = string_printf("asyncHttpDownload-%d", id++);

    return startAsync(tag, [host, path, outResponse](Awaitable * /* awtSelf */) {
        tcp::socket socket(io());
        
        size_t contentLength;
        doAsyncHttpGetHeader(socket, host, path, outResponse, contentLength);
        
        size_t numBytesTransferred;
        AwaitableHandle awt = asyncRead(socket, outResponse, asio::transfer_exactly(contentLength - outResponse->size()), numBytesTransferred);
        awt->await();
    });
}

} }
