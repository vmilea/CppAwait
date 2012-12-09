CppAwait
========

CppAwait is a portable library that brings the await operator from C# 5 to C++. It uses stackful coroutines (_Boost.Context_) instead of compiler magic to get the same job done.



What is await?
==============

In a nutshell, it's a way to compose asynchronous operations in sequential style (as opposed to continuation passing style).

Any algorithm that involves blocking operations can be turned asynchronous while keeping original control flow. This approach is well suited for writing responsive applications that have to deal with laggy network & file I/O.

A more in depth introduction is available on [MSDN](http://msdn.microsoft.com/en-us/library/hh191443.aspx).



How does it work in practice?
=============================

Download and archive some files, allowing for archival while next download is in progress:


    std::vector<std::string> urls = { ... };

    ut::AwaitableHandle awtArchive;

    for (std::string url : urls) {
        // holds fetched document
        std::unique_ptr<std::vector<char> > document;

        ut::AwaitableHandle awtFetch = asyncFetch(url, &document);

        // doesn't block, instead it yields. the coroutine
        // gets resumed when fetch done or on exception.
        awtFetch.await(); 

        if (awtArchive) {
            awtArchive.await();
        }
        awtArchive = asyncArchive(std::move(document));
    }


For more, check out the included [examples](/vmilea/CppAwait/tree/master/Examples)!



Features
========

- simple, clear code

- coroutines, wrapped generators

- composable awaitables

- trivial to write custom awaitables

- pluggable into any GUI framework

- exception propagation across coroutines

- good performance with stack pooling

- various logging levels

- portable

- debuggable (break into stacks as usual)



Getting started
===============

1. __Install Boost.__ To use CppAwait you need Boost 1.52+ with _Boost.Context_ and _Boost.System_ compiled. Examples additionally link to _Boost.Thread_, _Boost.Chrono_. Quick guide:

   - get archive from [here](http://www.boost.org/users/download/)
   - ./bootstrap
   - ./b2 --build-type=minimal --with-thread --with-chrono --with-context --toolset=your-toolset stage

2. __Install CMake 2.8+__ from [here](http://www.cmake.org/cmake/resources/software.html).

3. __Build CppAwait__:

   - mkdir build\_dir ; cd build\_dir
   - cmake -G "your-generator" -DBOOST\_INCLUDEDIR="path-to-boost" -DBOOST\_LIBRARYDIR="path-to-boost-libs" "path-to-CppAwait"
   - open solution / make



Portability
===========

The library is supported on Windows / Linux with any of these compilers:

   - Visual C++ 2010 or later
   - GCC 4.6 or later

Boost.Context includes assembly code for ARM / MIPS / PowerPC32 / PowerPC64 / X86-32 / X86-64.

Porting to additional platforms (e.g. iOS, Android) should be trivial.



How it works
============

### TODO

- describe stackful coroutines

- describe how context switches are orchestrated

- describe lifetime of stacks & awaitables



Authors
=======

Valentin Milea <valentin.milea@gmail.com>


License
=======

    Copyright 2012 Valentin Milea

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
