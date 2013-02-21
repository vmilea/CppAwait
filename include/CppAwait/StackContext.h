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
#include "impl/Functional.h"
#include "impl/Assert.h"
#include "impl/Compatibility.h"
#include <string>
#include <memory>
#include <stdexcept>

namespace ut {

//
// types
//

enum YieldType
{
    YT_RESULT,
    YT_EXCEPTION
};

//
// StackContext -- provides context for coroutines
//

class StackContext
{
public:
    static size_t minimumStackSize();
    static size_t maximumStackSize();
    static size_t defaultStackSize();
    static void setDefaultStackSize(size_t size);
    static void drainContextPool();

    typedef std::function<void (void *)> Coroutine;

    StackContext(const std::string& tag, Coroutine coroutine, size_t stackSize = defaultStackSize());
    StackContext(const std::string& tag, size_t stackSize = defaultStackSize());
    ~StackContext();

    StackContext(StackContext&& other);
    StackContext& operator=(StackContext&& other);

    operator bool();

    const char* tag();

    bool isRunning();
    void start(Coroutine coroutine);

    void* yield(void *value = nullptr);
    void* yieldTo(StackContext *resumeContext, void *value = nullptr);
    void* yieldException(std::exception_ptr eptr);
    void* yieldExceptionTo(StackContext *resumeContext, std::exception_ptr eptr);

    StackContext* parent();
    void setParent(StackContext *context);

private:
    static size_t sDefaultStackSize;

    static void contextFunc(intptr_t data);

    StackContext();
    StackContext(const StackContext& other);
    StackContext& operator=(const StackContext& other);

    void* implYieldTo(StackContext *resumeContext, YieldType type, void *value);

    struct Impl;
    std::unique_ptr<Impl> mImpl;

    friend void initMainContext();
};


//
// main/current context
//

void initMainContext();

StackContext* mainContext();

StackContext* currentContext();

//
// yield helpers
//

inline void* yield(void *value = nullptr)
{
    return currentContext()->yield(value);
}

inline void* yieldTo(StackContext *resumeContext, void *value = nullptr)
{
    return currentContext()->yieldTo(resumeContext, value);
}

inline void* yieldException(std::exception_ptr eptr)
{
    return currentContext()->yieldException(eptr);
}

inline void* yieldExceptionTo(StackContext *resumeContext, std::exception_ptr eptr)
{
    return currentContext()->yieldExceptionTo(resumeContext, eptr);
}

template <typename T>
void* yieldException(const T& e)
{
    return currentContext()->yieldException(ut::make_exception_ptr(e));
}

template <typename T>
void* yieldExceptionTo(StackContext *resumeContext, const T& e)
{
    return currentContext()->yieldExceptionTo(resumeContext, ut::make_exception_ptr(e));
}

//
// misc
//

class YieldForbidden
{
public:
    static std::exception_ptr ptr();
};

class ForcedUnwind
{
public:
    static std::exception_ptr ptr();
};

inline void forceUnwind(StackContext *context)
{
    try {
        yieldExceptionTo(context, ForcedUnwind::ptr());
    } catch (...) {
        ut_assert_(false && "stack context may not throw on ForcedUnwind");
    }
}

}
