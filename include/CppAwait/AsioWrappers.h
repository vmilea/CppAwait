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
#include "impl/CompletionGuard.h"
#include "impl/OpaqueSharedPtr.h"
#include <boost/asio.hpp>

// experimental Boost.Asio wrappers

// note: Asio requires some arguments to be valid until callback. This complicates cancellation:
//       sockets / buffers must be kept alive after calling socket.close() until the callbacks
//       have run on io_service.
//       For an interrupted Awaitable, it means the callback may run after its stack context
//       has been unwinded. Workaround: shared_ptr abuse.
//
// TODO: - check if Asio supports some kind of instant cancellation
//       - consider delaying unwind until callback

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
        CompletionGuard guard;
        auto guardToken = guard.getToken();

        resolver.async_resolve(query, [&, guardToken](const boost::system::error_code& ec, ResolverIterator it) {
            if (guardToken->isBlocked()) {
                return;
            }

            outEndpoints = it;

            if (!ec) {
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
        CompletionGuard guard;
        auto guardToken = guard.getToken();

        socket.async_connect(endpoint, [&, guardToken](const boost::system::error_code& ec) {
            if (guardToken->isBlocked()) {
                return;
            }

            if (!ec) {
                yieldTo(context);
            } else {
                yieldExceptionTo(context, boost::system::system_error(ec));
            }
        });

        yield();
    });
}


template <typename Socket, typename Iterator>
AwaitableHandle asyncConnect(Socket& socket, Iterator begin, Iterator& outConnected)
{
    return startAsync("asyncConnect", [&socket, begin, &outConnected](Awaitable * /* awtSelf */) {
        StackContext *context = currentContext();
        CompletionGuard guard;
        auto guardToken = guard.getToken();

        boost::asio::async_connect(socket, begin, [&, guardToken](const boost::system::error_code& ec, Iterator iterator) {
            if (guardToken->isBlocked()) {
                return;
            }

            outConnected = iterator;

            if (!ec) {
                yieldTo(context);
            } else {
                yieldExceptionTo(context, boost::system::system_error(ec));
            }
        });

        yield();
    });
}


template <typename Acceptor, typename PeerSocket>
AwaitableHandle asyncAccept(Acceptor& acceptor, std::shared_ptr<PeerSocket> peer)
{
    return startAsync("asyncAccept", [&acceptor, peer](Awaitable * /* awtSelf */) {
        StackContext *context = currentContext();
        CompletionGuard guard;
        auto guardToken = guard.getToken();

        acceptor.async_accept(*peer, [&, guardToken, peer](const boost::system::error_code& ec) {
            if (guardToken->isBlocked()) {
                return;
            }

            if (!ec) {
                yieldTo(context);
            } else {
                yieldExceptionTo(context, boost::system::system_error(ec));
            }
        });

        yield();
    });
}

template <typename Acceptor, typename PeerSocket>
AwaitableHandle asyncAccept(Acceptor& acceptor, std::shared_ptr<PeerSocket> peer, std::shared_ptr<typename Acceptor::endpoint_type>& peerEndpoint)
{
    return startAsync("asyncAccept", [&acceptor, peer, peerEndpoint](Awaitable * /* awtSelf */) {
        StackContext *context = currentContext();
        CompletionGuard guard;
        auto guardToken = guard.getToken();

        acceptor.async_accept(*peer, *peerEndpoint, [&, guardToken, peer, peerEndpoint](const boost::system::error_code& ec) {
            if (guardToken->isBlocked()) {
                return;
            }

            if (!ec) {
                yieldTo(context);
            } else {
                yieldExceptionTo(context, boost::system::system_error(ec));
            }
        });

        yield();
    });
}


template <typename AsyncWriteStream, typename ConstBufferSequence, typename CompletionCondition>
AwaitableHandle asyncWrite(AsyncWriteStream& stream, const ConstBufferSequence& buffers, OpaqueSharedPtr masterBuffer, CompletionCondition completionCondition, std::size_t& outBytesTransferred)
{
    return startAsync("asyncWrite", [&stream, buffers, masterBuffer, completionCondition, &outBytesTransferred](Awaitable * /* awtSelf */) {
        StackContext *context = currentContext();
        CompletionGuard guard;
        auto guardToken = guard.getToken();

        boost::asio::async_write(stream, buffers, completionCondition, [&, guardToken, masterBuffer](const boost::system::error_code& ec, std::size_t bytesTransferred) {
            if (guardToken->isBlocked()) {
                return;
            }

            outBytesTransferred = bytesTransferred;

            if (!ec) {
                yieldTo(context);
            } else {
                yieldExceptionTo(context, boost::system::system_error(ec));
            }
        });

        yield();
    });
}

template <typename AsyncWriteStream, typename ConstBufferSequence>
AwaitableHandle asyncWrite(AsyncWriteStream& stream, const ConstBufferSequence& buffers, OpaqueSharedPtr masterBuffer)
{
    static size_t bytesTransferred;
    return asyncWrite(stream, buffers, std::move(masterBuffer), boost::asio::transfer_all(), bytesTransferred);
}

template <typename AsyncWriteStream, typename Buffer>
AwaitableHandle asyncWrite(AsyncWriteStream& stream, std::shared_ptr<Buffer> buffer)
{
    return asyncWrite(stream, boost::asio::buffer(*buffer), OpaqueSharedPtr(buffer));
}


template <typename AsyncWriteStream, typename Allocator, typename CompletionCondition>
AwaitableHandle asyncWrite(AsyncWriteStream& stream, std::shared_ptr<boost::asio::basic_streambuf<Allocator> > buffer, CompletionCondition completionCondition, std::size_t& outBytesTransferred)
{
    return startAsync("asyncWrite", [&stream, buffer, completionCondition, &outBytesTransferred](Awaitable * /* awtSelf */) {
        StackContext *context = currentContext();
        CompletionGuard guard;
        auto guardToken = guard.getToken();

        boost::asio::async_write(stream, *buffer, completionCondition, [&, guardToken, buffer](const boost::system::error_code& ec, std::size_t bytesTransferred) {
            if (guardToken->isBlocked()) {
                return;
            }

            outBytesTransferred = bytesTransferred;

            if (!ec) {
                yieldTo(context);
            } else {
                yieldExceptionTo(context, boost::system::system_error(ec));
            }
        });

        yield();
    });
}

template <typename AsyncWriteStream, typename Allocator>
AwaitableHandle asyncWrite(AsyncWriteStream& stream, std::shared_ptr<boost::asio::basic_streambuf<Allocator> > buffer)
{
    static size_t bytesTransferred;
    return asyncWrite(stream, std::move(buffer), boost::asio::transfer_all(), bytesTransferred);
}


template <typename AsyncReadStream, typename MutableBufferSequence, typename CompletionCondition>
AwaitableHandle asyncRead(AsyncReadStream& stream, const MutableBufferSequence& outBuffers, OpaqueSharedPtr masterBuffer, CompletionCondition completionCondition, std::size_t& outBytesTransferred)
{
    return startAsync("asyncRead", [&stream, outBuffers, masterBuffer, completionCondition, &outBytesTransferred](Awaitable * /* awtSelf */) {
        StackContext *context = currentContext();
        CompletionGuard guard;
        auto guardToken = guard.getToken();

        boost::asio::async_read(stream, outBuffers, completionCondition, [&, guardToken, masterBuffer](const boost::system::error_code& ec, std::size_t bytesTransferred) {
            if (guardToken->isBlocked()) {
                return;
            }

            outBytesTransferred = bytesTransferred;

            if (!ec) {
                yieldTo(context);
            } else {
                yieldExceptionTo(context, boost::system::system_error(ec));
            }
        });

        yield();
    });
}

template <typename AsyncReadStream, typename MutableBufferSequence>
AwaitableHandle asyncRead(AsyncReadStream& stream, const MutableBufferSequence& outBuffer, OpaqueSharedPtr masterBuffer)
{
    static size_t bytesTransferred;
    return asyncRead(stream, outBuffer, std::move(masterBuffer), boost::asio::transfer_all(), bytesTransferred);
}

template <typename AsyncReadStream, typename Buffer>
AwaitableHandle asyncRead(AsyncReadStream& stream, std::shared_ptr<Buffer> buffer)
{
    return asyncRead(stream, boost::asio::buffer(*buffer), OpaqueSharedPtr(buffer));
}


template <typename AsyncReadStream, typename Allocator, typename CompletionCondition>
AwaitableHandle asyncRead(AsyncReadStream& stream, std::shared_ptr<boost::asio::basic_streambuf<Allocator> > outBuffer, CompletionCondition completionCondition, std::size_t& outBytesTransferred)
{
    return startAsync("asyncRead", [&stream, outBuffer, completionCondition, &outBytesTransferred](Awaitable * /* awtSelf */) {
        StackContext *context = currentContext();
        CompletionGuard guard;
        auto guardToken = guard.getToken();

        boost::asio::async_read(stream, *outBuffer, completionCondition, [&, guardToken, outBuffer](const boost::system::error_code& ec, std::size_t bytesTransferred) {
            if (guardToken->isBlocked()) {
                return;
            }

            outBytesTransferred = bytesTransferred;

            if (!ec) {
                yieldTo(context);
            } else {
                yieldExceptionTo(context, boost::system::system_error(ec));
            }
        });

        yield();
    });
}

template <typename AsyncReadStream, typename Allocator>
AwaitableHandle asyncRead(AsyncReadStream& stream, std::shared_ptr<boost::asio::basic_streambuf<Allocator> > outBuffer)
{
    static size_t bytesTransferred;
    return asyncRead(stream, std::move(outBuffer), boost::asio::transfer_all(), bytesTransferred);
}


template <typename AsyncReadStream, typename Allocator, typename Condition>
AwaitableHandle asyncReadUntil(AsyncReadStream& stream, std::shared_ptr<boost::asio::basic_streambuf<Allocator> > outBuffer, const Condition& condition, std::size_t& outBytesTransferred)
{
    return startAsync("asyncReadUntil", [&stream, outBuffer, condition, &outBytesTransferred](Awaitable * /* awtSelf */) {
        StackContext *context = currentContext();
        CompletionGuard guard;
        auto guardToken = guard.getToken();

        boost::asio::async_read_until(stream, *outBuffer, condition, [&, guardToken, outBuffer](const boost::system::error_code& ec, std::size_t bytesTransferred) {
            if (guardToken->isBlocked()) {
                return;
            }

            outBytesTransferred = bytesTransferred;

            if (!ec) {
                yieldTo(context);
            } else {
                yieldExceptionTo(context, boost::system::system_error(ec));
            }
        });

        yield();
    });
}

template <typename AsyncReadStream, typename Allocator, typename Condition>
AwaitableHandle asyncReadUntil(AsyncReadStream& stream, std::shared_ptr<boost::asio::basic_streambuf<Allocator> > outBuffer, const Condition& condition)
{
    static size_t bytesTransferred;
    return asyncReadUntil(stream, std::move(outBuffer), condition, bytesTransferred);
}


AwaitableHandle asyncHttpDownload(const std::string& host, const std::string& path, std::shared_ptr<boost::asio::streambuf> outResponse);

} }
