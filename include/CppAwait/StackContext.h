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

/**
 * @file  StackContext.h
 *
 * Declares the StackContext class and various helper functions.
 *
 */

 #pragma once

#include "Config.h"
#include "impl/Functional.h"
#include "impl/Assert.h"
#include "impl/Compatibility.h"
#include <string>
#include <memory>
#include <stdexcept>

/** CppAwait namespace */
namespace ut {

/**
 * Basic coroutine abstraction
 *
 * StackContext is a simple implementation of stackful coroutines on top of Boost.Context.
 *
 * Coroutines are a form of cooperative multitasking. At various points during its execution
 * a coroutine may decide to yield control to another. The yielding coroutine is suspended
 * with its stack intact and a snapshot of cpu registers. Eventually someone yields back, the
 * original coroutine has its local state restored and it resumes.
 *
 * The library will associate a default StackContext with main program stack. Users may start
 * additional contexts to hold coroutines. Each context remembers its parent (by default the
 * context which started it). When its coroutine finishes, the context will resume parent. Note,
 * in general there are no restrictions -- a context may yield to any other at any time.
 *
 * It is possible to yield both regular values and exceptions. To keep thing simple values are
 * passed as raw pointers. It's safe to access local variables from inside the coroutine while
 * it is suspended.
 *
 * Exception are passed as std::exception_ptr and get thrown on receiving end. There is no limit
 * on the number of exceptions yielded as long as they don't cause coroutine to finish. Uncaught
 * exceptions pop back in parent context (with the exception of ForcedUnwind which is silently
 * swallowed).
 *
 * The size of a context's stack can be configured. Complex coroutines may need extra space. Also
 * consider adjusting the default stack size, which is platform dependent. Note that stack usage
 * varies according to compiler flags & version.
 *
 * StackContexts can be tagged to ease debugging.
 *
 * @warning Not thread safe. Coroutines are designed for single-threaded use.
 *
 */
class StackContext
{
public:
    /**
     * Coroutine signature
     *
     * Any uncaught exception will pop out on parent context, except ForcedUnwind
     * which is silently swallowed.
     *
     * @param value   initial value yielded to coroutine
     */
    typedef std::function<void (void *value)> Coroutine;

    /** Minimum stack size allowed on current platform */
    static size_t minimumStackSize();

    /** Maximum stack size allowed on current platform */
    static size_t maximumStackSize();

    /** Default stack size on current platform */
    static size_t defaultStackSize();

    /** Change default stack size for new contexts */
    static void setDefaultStackSize(size_t size);

    /** Discard cached stack buffers */
    static void drainContextPool();

    /**
     * Create a stack context and start coroutine
     * @param tag        identifier for debugging
     * @param coroutine  function that may yield()
     * @param stackSize  size of context stack
     */
    StackContext(const std::string& tag, Coroutine coroutine, size_t stackSize = defaultStackSize());

    /**
     * Create a stack context
     * @param tag        identifier for debugging
     * @param stackSize  size of context stack
     */
    StackContext(const std::string& tag, size_t stackSize = defaultStackSize());

    /** Destroy stack context. It is illegal to call the destructor of a running context */
    ~StackContext();

    /** Move contructor */
    StackContext(StackContext&& other);

    /** Move assignment */
    StackContext& operator=(StackContext&& other);

    /** Identifier for debugging */
    const char* tag();

    /** Returns true if coroutine is running */
    bool isRunning();

    /** Starts context. Note, coroutine body is not entered until resumed via yield() */
    void start(Coroutine coroutine);

    /**
     * Suspend self, return value to parent context
     * @param   value          data to yield
     * @return  a value or exception
     */
    void* yield(void *value = nullptr);

    /**
     * Suspend self, return value to given context
     * @param   resumeContext  context to resume
     * @param   value          data to yield
     * @return  a value or exception
     */
    void* yieldTo(StackContext *resumeContext, void *value = nullptr);

    /**
     * Suspend self, throw exception on parent context
     * @param   eptr           exception to throw on parent context
     * @return  a value or exception
     */
    void* yieldException(std::exception_ptr eptr);

    /**
     * Suspend self, throw exception on given context
     * @param   resumeContext  context to resume
     * @param   eptr           exception to throw on context
     * @return  a value or exception
     */
    void* yieldExceptionTo(StackContext *resumeContext, std::exception_ptr eptr);

    /** Context to yield to by default */
    StackContext* parent();

    /** Set parent context */
    void setParent(StackContext *context);

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

    static void contextFunc(intptr_t data);

    StackContext();
    StackContext(const StackContext& other); // noncopyable
    StackContext& operator=(const StackContext& other);  // noncopyable

    void* implYieldTo(StackContext *resumeContext, YieldType type, void *value);

    struct Impl;
    std::unique_ptr<Impl> mImpl;

    friend void initMainContext();
};


//
// main/current context
//

/** Initialize library. Must be called once from main stack to setup main context. */
void initMainContext();

/** Returns the main context */
StackContext* mainContext();

/** Returns the current context */
StackContext* currentContext();

//
// yield helpers
//

/** Helper function, yields from current context */
inline void* yield(void *value = nullptr)
{
    return currentContext()->yield(value);
}

/** Helper function, yields from current context */
inline void* yieldTo(StackContext *resumeContext, void *value = nullptr)
{
    return currentContext()->yieldTo(resumeContext, value);
}

/** Helper function, yields from current context */
inline void* yieldException(std::exception_ptr eptr)
{
    return currentContext()->yieldException(eptr);
}

/** Helper function, yields from current context */
inline void* yieldExceptionTo(StackContext *resumeContext, std::exception_ptr eptr)
{
    return currentContext()->yieldExceptionTo(resumeContext, eptr);
}

/**
 * Helper function, packs an exception and yields from current context
 *
 * @warning  Don't call this while an exception is propagating
 *           as it relies on a buggy make_exception_ptr
 */
template <typename T>
void* yieldException(const T& e)
{
    return currentContext()->yieldException(ut::make_exception_ptr(e));
}

/**
 * Helper function, packs an exception and yields from current context
 *
 * @warning  Don't call this while an exception is propagating
 *           as it relies on a buggy make_exception_ptr
 */
template <typename T>
void* yieldExceptionTo(StackContext *resumeContext, const T& e)
{
    return currentContext()->yieldExceptionTo(resumeContext, ut::make_exception_ptr(e));
}

//
// misc
//

/** Thrown in case the yielded-to context is no longer valid */
class YieldForbidden
{
public:
    static std::exception_ptr ptr();
};

/** Special exception used to interrupt coroutines */
class ForcedUnwind
{
public:
    static std::exception_ptr ptr();
};

/**
 * Helper function, yields ForcedUnwind exception to a context.
 *
 * Safe for use while an exception is propagating.
 */
inline void forceUnwind(StackContext *context)
{
    try {
        yieldExceptionTo(context, ForcedUnwind::ptr());
    } catch (...) {
        ut_assert_(false && "stack context may not throw on ForcedUnwind");
    }
}

}
