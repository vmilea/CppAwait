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

/**
 * @file  Coro.h
 *
 * Declares the Coro class and various helper functions.
 *
 */

 #pragma once

#include "Config.h"
#include "misc/Functional.h"
#include "impl/Compatibility.h"
#include <string>
#include <memory>
#include <stdexcept>

/** CppAwait namespace */
namespace ut {

/**
 * Basic coroutine abstraction
 *
 * Coro supports stackful coroutines on top of Boost.Context.
 *
 * Coroutines are a form of cooperative multitasking. At various points during its execution
 * a coroutine may decide to yield control to another. The yielding coroutine is suspended with
 * its stack intact and a snapshot of cpu registers, until some other coroutine yields back to it.
 *
 * The library will associate a default Coro with main program stack. Additional coroutines can
 * be started. Each new Coro remembers its parent and will yield to it when done. Note, in general
 * there are no restrictions -- a coroutine may yield to any other at any time.
 *
 * It is possible to yield both regular values and exceptions. To keep thing simple values are
 * passed as raw pointers. It's safe to access local variables from inside the coroutine while
 * it is suspended.
 *
 * Exception are passed as std::exception_ptr and get thrown on receiving end. There is no limit
 * on the number of exceptions yielded as long as they don't cause coroutine to finish. Uncaught
 * exceptions pop back in parent coroutine (with the exception of ForcedUnwind which is silently
 * swallowed).
 *
 * Stack size can be configured per Coro. Complex coroutines may need extra space. Also consider
 * adjusting the default stack size, which is platform dependent. Note that actual stack usage
 * varies -- debug builds usually need larger stacks.
 *
 * Coros can be tagged to ease debugging.
 *
 * @warning Not thread safe. Coroutines are designed for single-threaded use.
 *
 */
class Coro
{
public:
    /**
     * Coroutine body signature
     *
     * Any uncaught exception will pop out on parent coroutine,
     * except ForcedUnwind which is silently swallowed.
     *
     * @param value   initial value yielded to coroutine
     */
    typedef std::function<void (void *value)> Func;

    /** Minimum stack size allowed on current platform */
    static size_t minimumStackSize();

    /** Maximum stack size allowed on current platform */
    static size_t maximumStackSize();

    /** Default stack size on current platform */
    static size_t defaultStackSize();

    /** Change default stack size for new stacks */
    static void setDefaultStackSize(size_t size);

    /** Discard cached stack buffers */
    static void drainStackPool();

    /**
     * Create and initialize a coroutine
     * @param tag        identifier for debugging
     * @param func       coroutine body, may yield()
     * @param stackSize  size of stack
     */
    Coro(std::string tag, Func func, size_t stackSize = defaultStackSize());

    /**
     * Create a coroutine
     * @param tag        identifier for debugging
     * @param stackSize  size of stack
     */
    Coro(std::string tag, size_t stackSize = defaultStackSize());

    /** Destroy coroutine. It is illegal to call the destructor of a running coroutine */
    ~Coro();

    /** Move contructor */
    Coro(Coro&& other);

    /** Move assignment */
    Coro& operator=(Coro&& other);

    /** Identifier for debugging */
    const char* tag();

    /** Returns true after init() until func returns */
    bool isRunning();

    /** Initialize coroutine. Note, func is not entered until resumed via yield() */
    void init(Func func);

    /**
     * Suspend self, return value to parent coroutine
     * @param   value       data to yield
     * @return  a value or exception
     */
    void* yield(void *value = nullptr);

    /**
     * Suspend self, return value to given coroutine
     * @param   resumeCoro  coroutine to resume
     * @param   value       data to yield
     * @return  a value or exception
     */
    void* yieldTo(Coro *resumeCoro, void *value = nullptr);

    /**
     * Suspend self, throw exception on parent coroutine
     * @param   eptr        exception to throw on parent coroutine
     * @return  a value or exception
     */
    void* yieldException(std::exception_ptr eptr);

    /**
     * Suspend self, throw exception on given coroutine
     * @param   resumeCoro  coroutine to resume
     * @param   eptr        exception to throw on coroutine
     * @return  a value or exception
     */
    void* yieldExceptionTo(Coro *resumeCoro, std::exception_ptr eptr);

    /** Coroutine to yield to by default */
    Coro* parent();

    /** Set parent coroutine */
    void setParent(Coro *coro);

private:
    enum YieldType
    {
        YT_RESULT,
        YT_EXCEPTION
    };

    struct YieldValue
    {
        YieldType type;
        void *value;

        YieldValue(YieldType type, void *value)
            : type(type), value(value) { }
    };

    static size_t sDefaultStackSize;

    static void fcontextFunc(intptr_t data);

    Coro();
    Coro(const Coro& other); // noncopyable
    Coro& operator=(const Coro& other); // noncopyable

    void* implYieldTo(Coro *resumeCoro, YieldType type, void *value);
    void* unpackYieldValue(const YieldValue& yReceived);

    struct Impl;
    std::unique_ptr<Impl> m;

    friend void initCoroLib();
};


//
// master/current coroutine
//

/** Initialize coroutine library. Must be called once from main stack. */
void initCoroLib();

/** Returns the current coroutine */
Coro* currentCoro();

/** Returns the master coroutine */
Coro* masterCoro();

/** Temporarily makes current coroutine the master */
class PushMasterCoro
{
public:
    PushMasterCoro();
    ~PushMasterCoro();

private:
    PushMasterCoro(const PushMasterCoro& other); // noncopyable
    PushMasterCoro& operator=(const PushMasterCoro& other); // noncopyable

    Coro *mPushedCoro;
};

//
// yield helpers
//

/** Helper function, yields from current coroutine */
inline void* yield(void *value = nullptr)
{
    void *received = currentCoro()->yield(value);
    return received;
}

/** Helper function, yields from current coroutine */
inline void* yieldTo(Coro *resumeCoro, void *value = nullptr)
{
    void *received = currentCoro()->yieldTo(resumeCoro, value);
    return received;
}

/** Helper function, yields from current coroutine */
inline void* yieldException(std::exception_ptr eptr)
{
    void *received = currentCoro()->yieldException(std::move(eptr));
    return received;
}

/** Helper function, yields from current coroutine */
inline void* yieldExceptionTo(Coro *resumeCoro, std::exception_ptr eptr)
{
    void *received = currentCoro()->yieldExceptionTo(resumeCoro, std::move(eptr));
    return received;
}

/**
 * Helper function, packs an exception and yields from current coroutine
 *
 * @warning  Don't call this while an exception is propagating
 *           as it relies on a buggy make_exception_ptr
 */
template <typename T>
void* yieldException(const T& e)
{
    void *received = currentCoro()->yieldException(ut::make_exception_ptr(e));
    return received;
}

/**
 * Helper function, packs an exception and yields from current coroutine
 *
 * @warning  Don't call this while an exception is propagating
 *           as it relies on a buggy make_exception_ptr
 */
template <typename T>
void* yieldExceptionTo(Coro *resumeCoro, const T& e)
{
    void *received = currentCoro()->yieldExceptionTo(resumeCoro, ut::make_exception_ptr(e));
    return received;
}

//
// misc
//

/** Thrown on attempting yield to an invalid coroutine */
class YieldForbidden
{
public:
    static std::exception_ptr ptr();
};

/** Special exception for interrupting a coroutine */
class ForcedUnwind
{
public:
    static std::exception_ptr ptr();
};

/**
 * Helper function, yields ForcedUnwind exception to a coroutine.
 *
 * Safe for use while an exception is propagating.
 */
void forceUnwind(Coro *coro);

}
