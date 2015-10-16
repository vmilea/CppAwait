## NOTICE

### **This library is no longer being maintained. All development has shifted to [CppAsync](https://github.com/vmilea/CppAsync), a complete reimplementation that is more efficient, flexible, and doesn't depend on Boost.Context.**

CppAwait
========

CppAwait is a C++ library that enables writing asynchronous code in a natural (sequential) form.

It solves the code flow inversion typical of asynchronous APIs. So -- instead of chaining callbacks, dealing with state and error codes -- you can use plain conditionals, loops and exception handlers to express your asynchronous algorithm.

The goal: make it easier to write responsive applications that have to deal with laggy network & file I/O.


How it works
============

CppAwait provides an _await_ construct similar to the one from C# 5 (see [MSDN](http://msdn.microsoft.com/en-us/library/hh191443.aspx)). Upon reaching an _await_ expression the C# compiler automatically transforms the rest of the method into a task continuation. In C++ a similar effect can be achieved through coroutines (which CppAwait implements on top of Boost.Context). Calling `await()` will suspend a coroutine until the associated task completes. The program is free to do other work while coroutine is suspended.

Your program needs a run loop to manage asynchronous operations.


Here is a snippet showing typical library use. It connects and transfers some data to a TCP server:

    ut::Awaitable awt;

    tcp::resolver::iterator outEndpoints;
    awt = ut::asio::asyncResolve(resolver, query, outEndpoints);
    // Suspend coroutine until task has finished
    awt.await();

    for (auto it = outEndpoints; it != tcp::resolver::iterator(); ++it) {
        tcp::endpoint ep = *it;
        awt = ut::asio::asyncConnect(socket, ep);

        try {
            awt.await();
            break; // connected
        } catch (const std::exception&) {
            // Try next endpoint
        }
    }
    if (!socket.is_open()) {
        throw SocketError("failed to connect socket");
    }

    // Write an HTTP request
    awt = ut::asio::asyncWrite(socket, request);
    awt.await();

    // Read response
    awt = ut::asio::asyncReadUntil(socket, outResponse, std::string("\r\n"));
    awt.await();

The `Awaitable` class is a generic wrapper for asynchronous operations. All asynchronous methods return an `Awaitable`. Its `await()` method yields control to the main loop until the `Awaitable` completes or fails, at which point the coroutine resumes. If operation failed the associated exception gets thrown. Note there is no return value -- output parameters such as _endpoints_ must be passed by reference.

`Awaitables` compose easily, just like regular functions. More complex patterns are supported through `awaitAll()` / `awaitAny()`.


Here is another snippet which downloads and archives some files. While a file is being archived, the next one is being downloaded:

    std::vector<std::string> urls = { ... };

    ut::Awaitable awtArchive;

    for (std::string url : urls) {
        // Holds fetched document
        std::unique_ptr<std::vector<char> > document;

        ut::Awaitable awtFetch = asyncFetch(url, &document);

        // Doesn't block, instead it yields. The coroutine
        // gets resumed when fetch done or on exception.
        awtFetch.await();

        if (!awtArchive.isNil()) {
            awtArchive.await();
        }
        awtArchive = asyncArchive(std::move(document));
    }


There are several [examples](/Examples) included. See [stock client](/Examples/ex_stockClient.cpp) for a direct comparison between classic async and the await pattern.


Features
========

- coroutines, iterable generators

- composable awaitables

- support for exceptions

- can adapt any asynchronous API

- _Boost.Asio_ wrappers

- pluggable into any program with a run loop

- good performance with stack pooling

- customizable logging

- portable


Installation
============

1. __Install [Boost](http://www.boost.org/users/download/).__ To use CppAwait you need Boost 1.58+ with _Boost.Context_, _Boost.Thread_ and _Boost.System_ compiled [[*]](#deps). Quick guide:

   - download Boost archive
   - `./bootstrap`
   - `./b2 link=static --build-type=minimal --with-context --with-chrono --with-thread --toolset=your-toolset stage`

2. __Install [CMake](http://www.cmake.org/cmake/resources/software.html) 2.8+__

3. __Build CppAwait__:

   - `mkdir build_dir ; cd build_dir`
   - `cmake -G "your-generator" -DBOOST_ROOT="path-to-boost" "path-to-CppAwait"`
   - open solution / make

<a id="deps">(*)</a> _Boost.Chrono_ is additionally required to compile the examples on Visual C++ 2010. There is an optional Flickr example which depends on OpenSSL. Place OpenSSL under `C:\OpenSSL` for automatic detection on Windows.


Portability
===========

The library is supported on Windows / Linux with any of these compilers:

   - Visual C++ 2010 or later
   - GCC 4.6.2 or later

_Boost.Context_ includes assembly code for ARM / MIPS / PowerPC32 / PowerPC64 / X86-32 / X86-64.

Porting to additional platforms (e.g. iOS, Android) should be trivial.



Authors
=======

Valentin Milea <valentin.milea@gmail.com>


License
=======

    Copyright 2012-2015 Valentin Milea

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.