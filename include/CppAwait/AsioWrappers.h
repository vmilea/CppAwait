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
#include "OpaqueSharedPtr.h"
#include "impl/CallbackGuard.h"
#include <boost/asio.hpp>

// experimental Boost.Asio wrappers

// note: Asio requires some arguments to be valid until callback. This complicates cancellation:
//       sockets / buffers must be kept alive after calling socket.close() until the callbacks
//       have run on io_service.
//       For an interrupted Awaitable, it means the callback may run after its stack context
//       has been unwinded.
//
//       Workaround:
//         - CallbackWrapper ignores late callbacks
//         - callback lambda captures shared_ptr arguments
//
// TODO: - check if Asio supports some kind of instant cancellation
//       - consider delaying unwind until callback

namespace ut { namespace asio {

inline boost::asio::io_service& io()
{
    static boost::asio::io_service io;
    return io;
}

inline std::exception_ptr eptr(const boost::system::error_code& ec)
{
    if (ec) {
        return ut::make_exception_ptr(boost::system::system_error(ec));
    } else {
        return std::exception_ptr();
    }
}


template <typename Resolver>
inline AwaitableHandle asyncResolve(Resolver& resolver, const typename Resolver::query& query, typename Resolver::iterator& outEndpoints)
{
    typedef typename Resolver::iterator ResolverIterator;

    auto awt = new ut::Completable("asyncResolve");

    resolver.async_resolve(query,
                           awt->wrap([&](const boost::system::error_code& ec, ResolverIterator it) -> std::exception_ptr {
        outEndpoints = it;
        return eptr(ec);
    }));

    return AwaitableHandle(awt);
}


template <typename Socket>
inline AwaitableHandle asyncConnect(Socket& socket, const typename Socket::endpoint_type& endpoint)
{
    auto awt = new ut::Completable("asyncConnect");

    socket.async_connect(endpoint,
                         awt->wrap([](const boost::system::error_code& ec) -> std::exception_ptr {
        return eptr(ec);
    }));

    return AwaitableHandle(awt);
}


template <typename Socket, typename Iterator>
inline AwaitableHandle asyncConnect(Socket& socket, Iterator begin, Iterator& outConnected)
{
    auto awt = new ut::Completable("asyncConnect");

    boost::asio::async_connect(socket, begin,
                               awt->wrap([&](const boost::system::error_code& ec, Iterator iterator) -> std::exception_ptr {
        outConnected = iterator;
        return eptr(ec);
    }));

    return AwaitableHandle(awt);
}


template <typename Acceptor, typename PeerSocket>
inline AwaitableHandle asyncAccept(Acceptor& acceptor, std::shared_ptr<PeerSocket> peer)
{
    auto awt = new ut::Completable("asyncAccept");

    acceptor.async_accept(*peer,
                          awt->wrap([peer](const boost::system::error_code& ec) -> std::exception_ptr {
        return eptr(ec);
    }));

    return AwaitableHandle(awt);
}

template <typename Acceptor, typename PeerSocket>
inline AwaitableHandle asyncAccept(Acceptor& acceptor, std::shared_ptr<PeerSocket> peer, std::shared_ptr<typename Acceptor::endpoint_type> peerEndpoint)
{
    auto awt = new ut::Completable("asyncAccept");

    acceptor.async_accept(*peer, *peerEndpoint,
                          awt->wrap([peer, peerEndpoint](const boost::system::error_code& ec) {
        return eptr(ec);
    }));

    return AwaitableHandle(awt);
}


template <typename AsyncWriteStream, typename ConstBufferSequence, typename CompletionCondition>
inline AwaitableHandle asyncWrite(AsyncWriteStream& stream, const ConstBufferSequence& buffers, OpaqueSharedPtr masterBuffer, CompletionCondition completionCondition, std::size_t& outBytesTransferred)
{
    auto awt = new ut::Completable("asyncWrite");

    boost::asio::async_write(stream, buffers, completionCondition,
                             awt->wrap([&, masterBuffer](const boost::system::error_code& ec, std::size_t bytesTransferred) -> std::exception_ptr {
        outBytesTransferred = bytesTransferred;
        return eptr(ec);
    }));

    return AwaitableHandle(awt);
}

template <typename AsyncWriteStream, typename ConstBufferSequence>
inline AwaitableHandle asyncWrite(AsyncWriteStream& stream, const ConstBufferSequence& buffers, OpaqueSharedPtr masterBuffer)
{
    static size_t bytesTransferred;
    return asyncWrite(stream, buffers, std::move(masterBuffer), boost::asio::transfer_all(), bytesTransferred);
}

template <typename AsyncWriteStream, typename Buffer>
inline AwaitableHandle asyncWrite(AsyncWriteStream& stream, std::shared_ptr<Buffer> buffer)
{
    return asyncWrite(stream, boost::asio::buffer(*buffer), OpaqueSharedPtr(buffer));
}

template <typename AsyncWriteStream, typename Allocator, typename CompletionCondition>
inline AwaitableHandle asyncWrite(AsyncWriteStream& stream, std::shared_ptr<boost::asio::basic_streambuf<Allocator> > buffer, CompletionCondition completionCondition, std::size_t& outBytesTransferred)
{
    auto awt = new ut::Completable("asyncWrite");

    boost::asio::async_write(stream, *buffer, completionCondition,
                             awt->wrap([&, buffer](const boost::system::error_code& ec, std::size_t bytesTransferred) -> std::exception_ptr {
        outBytesTransferred = bytesTransferred;
        return eptr(ec);
    }));

    return AwaitableHandle(awt);
}

template <typename AsyncWriteStream, typename Allocator>
inline AwaitableHandle asyncWrite(AsyncWriteStream& stream, std::shared_ptr<boost::asio::basic_streambuf<Allocator> > buffer)
{
    static size_t bytesTransferred;
    return asyncWrite(stream, std::move(buffer), boost::asio::transfer_all(), bytesTransferred);
}


template <typename AsyncReadStream, typename MutableBufferSequence, typename CompletionCondition>
inline AwaitableHandle asyncRead(AsyncReadStream& stream, const MutableBufferSequence& outBuffers, OpaqueSharedPtr masterBuffer, CompletionCondition completionCondition, std::size_t& outBytesTransferred)
{
    auto awt = new ut::Completable("asyncRead");

    boost::asio::async_read(stream, outBuffers, completionCondition,
                            awt->wrap([&, masterBuffer](const boost::system::error_code& ec, std::size_t bytesTransferred) -> std::exception_ptr {
        outBytesTransferred = bytesTransferred;
        return eptr(ec);
    }));

    return AwaitableHandle(awt);
}

template <typename AsyncReadStream, typename MutableBufferSequence>
inline AwaitableHandle asyncRead(AsyncReadStream& stream, const MutableBufferSequence& outBuffer, OpaqueSharedPtr masterBuffer)
{
    static size_t bytesTransferred;
    return asyncRead(stream, outBuffer, std::move(masterBuffer), boost::asio::transfer_all(), bytesTransferred);
}

template <typename AsyncReadStream, typename Buffer>
inline AwaitableHandle asyncRead(AsyncReadStream& stream, std::shared_ptr<Buffer> buffer)
{
    return asyncRead(stream, boost::asio::buffer(*buffer), OpaqueSharedPtr(buffer));
}

template <typename AsyncReadStream, typename Allocator, typename CompletionCondition>
inline AwaitableHandle asyncRead(AsyncReadStream& stream, std::shared_ptr<boost::asio::basic_streambuf<Allocator> > outBuffer, CompletionCondition completionCondition, std::size_t& outBytesTransferred)
{
    auto awt = new ut::Completable("asyncRead");

    boost::asio::async_read(stream, *outBuffer, completionCondition,
                            awt->wrap([&, outBuffer](const boost::system::error_code& ec, std::size_t bytesTransferred) -> std::exception_ptr {
        outBytesTransferred = bytesTransferred;
        return eptr(ec);
    }));

    return AwaitableHandle(awt);
}

template <typename AsyncReadStream, typename Allocator>
inline AwaitableHandle asyncRead(AsyncReadStream& stream, std::shared_ptr<boost::asio::basic_streambuf<Allocator> > outBuffer)
{
    static size_t bytesTransferred;
    return asyncRead(stream, std::move(outBuffer), boost::asio::transfer_all(), bytesTransferred);
}

template <typename AsyncReadStream, typename Allocator, typename Condition>
inline AwaitableHandle asyncReadUntil(AsyncReadStream& stream, std::shared_ptr<boost::asio::basic_streambuf<Allocator> > outBuffer, const Condition& condition, std::size_t& outBytesTransferred)
{
    auto awt = new ut::Completable("asyncReadUntil");

    boost::asio::async_read_until(stream, *outBuffer, condition,
                                  awt->wrap([&, outBuffer](const boost::system::error_code& ec, std::size_t bytesTransferred) -> std::exception_ptr {
        outBytesTransferred = bytesTransferred;
        return eptr(ec);
    }));

    return AwaitableHandle(awt);
}

template <typename AsyncReadStream, typename Allocator, typename Condition>
inline AwaitableHandle asyncReadUntil(AsyncReadStream& stream, std::shared_ptr<boost::asio::basic_streambuf<Allocator> > outBuffer, const Condition& condition)
{
    static size_t bytesTransferred;
    return asyncReadUntil(stream, std::move(outBuffer), condition, bytesTransferred);
}


AwaitableHandle asyncHttpDownload(const std::string& host, const std::string& path, std::shared_ptr<boost::asio::streambuf> outResponse);

} }
