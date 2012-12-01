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

#pragma once

#include "Config.h"
#include "Awaitable.h"
#include <boost/asio.hpp>

// experimental boost ASIO wrappers

namespace ut { namespace asio {

inline boost::asio::io_service& io()
{
    static boost::asio::io_service io;
    return io;
}

template <typename Resolver>
AwaitableHandle asyncResolve(Resolver& resolver, const typename Resolver::query& query, typename Resolver::iterator& outEndpoints)
{
    typedef typename Resolver::iterator ResolverIterator;

    return startAsync("asyncResolve", [&resolver, query, &outEndpoints](Awaitable * /* awtSelf */) {
        StackContext *context = currentContext();

        resolver.async_resolve(query, [&](const boost::system::error_code& ec, ResolverIterator it) {
            outEndpoints = it;
            
            if (ec.value() == 0) {
                yieldTo(context);
            } else {
                yieldExceptionTo(context, boost::system::system_error(ec));
            }
        });

        yield();
    });
}

template <typename Socket>
AwaitableHandle asyncConnect(Socket& socket, const typename Socket::endpoint_type& endpoint)
{
    return startAsync("asyncConnect", [&socket, endpoint](Awaitable * /* awtSelf */) {
        StackContext *context = currentContext();
        
        socket.async_connect(endpoint, [&](const boost::system::error_code& ec) {
            if (ec.value() == 0) {
                yieldTo(context);
            } else {
                yieldExceptionTo(context, boost::system::system_error(ec));
            }
        });

        yield();
    });
}

template <typename AsyncWriteStream, typename ConstBuffer>
AwaitableHandle asyncWrite(AsyncWriteStream& stream, ConstBuffer& buffer, std::size_t& outBytesTransferred)
{
    return startAsync("asyncWrite", [&stream, &buffer, &outBytesTransferred](Awaitable * /* awtSelf */) {
        StackContext *context = currentContext();

        boost::asio::async_write(stream, buffer, [&](const boost::system::error_code& ec, std::size_t bytesTransferred) {
            outBytesTransferred = bytesTransferred;
            
            if (ec.value() == 0) {
                yieldTo(context);
            } else {
                yieldExceptionTo(context, boost::system::system_error(ec));
            }
        }); 

        yield();
    });
}

template <typename AsyncReadStream, typename MutableBuffer>
AwaitableHandle asyncRead(AsyncReadStream& stream, MutableBuffer& outBuffer, std::size_t& outBytesTransferred)
{
    return startAsync("asyncRead", [&stream, &outBuffer, &outBytesTransferred](Awaitable * /* awtSelf */) {
        StackContext *context = currentContext();

        boost::asio::async_read(stream, outBuffer, [&](const boost::system::error_code& ec, std::size_t bytesTransferred) {
            outBytesTransferred = bytesTransferred;
            
            if (ec.value() == 0) {
                yieldTo(context);
            } else {
                yieldExceptionTo(context, boost::system::system_error(ec));
            }
        }); 

        yield();
    });
}

template <typename AsyncReadStream, typename MutableBuffer, typename CompletionCondition>
AwaitableHandle asyncRead(AsyncReadStream& stream, MutableBuffer& outBuffer, CompletionCondition completionCondition, std::size_t& outBytesTransferred)
{
    return startAsync("asyncRead", [&stream, &outBuffer, completionCondition, &outBytesTransferred](Awaitable * /* awtSelf */) {
        StackContext *context = currentContext();

        boost::asio::async_read(stream, outBuffer, completionCondition, [&](const boost::system::error_code& ec, std::size_t bytesTransferred) {
            outBytesTransferred = bytesTransferred;

            if (ec.value() == 0) {
                yieldTo(context);
            } else {
                yieldExceptionTo(context, boost::system::system_error(ec));
            }
        }); 

        yield();
    });
}

template <typename AsyncReadStream, typename MutableBuffer, typename MatchCondition>
AwaitableHandle asyncReadUntil(AsyncReadStream& stream, MutableBuffer& outBuffer, const MatchCondition& matchCondition, std::size_t& outBytesTransferred)
{
    return startAsync("asyncReadUntil", [&stream, &outBuffer, matchCondition, &outBytesTransferred](Awaitable * /* awtSelf */) {
        StackContext *context = currentContext();

        boost::asio::async_read_until(stream, outBuffer, matchCondition, [&](const boost::system::error_code& ec, std::size_t bytesTransferred) {
            outBytesTransferred = bytesTransferred;
            
            if (ec.value() == 0) {
                yieldTo(context);
            } else {
                yieldExceptionTo(context, boost::system::system_error(ec));
            }
        }); 

        yield();
    });
}

AwaitableHandle asyncHttpDownload(const std::string& host, const std::string& path, boost::asio::streambuf& outResponse);

} }
