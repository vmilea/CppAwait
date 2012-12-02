CppAwait
========

CppAwait is a portable library that brings the await operator from C# 5 to C++. It uses stackful coroutines (Boost.Context) instead of compiler magic to get the same job done.



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



Dependencies
============

C++11 compiler. Tested on Visual C++ 2010, Visual C++ 2012, GCC 4.6+

Boost 1.52. CppAwait must link to Boost.Context. Examples also need Boost.Thread.



Portability
===========

Boost.Context runs on ARM / MIPS / PowerPC32 / PowerPC64 / X86-32 / X86-64.



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
