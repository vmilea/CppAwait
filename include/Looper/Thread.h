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

#include <CppAwait/Config.h>
#include <CppAwait/impl/Functional.h>
#include "Chrono.h"
#include <utility>
#include <boost/thread.hpp>

namespace loo {

typedef boost::thread_interrupted ThreadInterrupted;

class TimeoutException { };

//
// this_thread namespace
//

namespace this_thread
{
    inline uint32_t id()
    {
        boost::thread::id threadId = boost::this_thread::get_id();
        uint32_t id;
        memcpy(&id, &threadId, 4);
        return id;
    }

    inline void yield()
    {
        boost::this_thread::yield();
    }

    inline void sleep(int32_t milliseconds)
    {
        boost::this_thread::sleep_for(boost::chrono::milliseconds(milliseconds));
    }

    inline void interruptionPoint()
    {
        boost::this_thread::interruption_point();
    }

    inline bool isInterruptionRequested()
    {
        return boost::this_thread::interruption_requested();
    }

    inline bool isInterruptionEnabled()
    {
        return boost::this_thread::interruption_enabled();
    }
}

//
// thread
//

class Thread
{
public:
    explicit Thread(ut::Runnable work)
        : mThread(work) { }

    uint32_t id()
    {
        boost::thread::id threadId = mThread.get_id();
        uint32_t id;
        memcpy(&id, &threadId, 4);
        return id;
    }
    
    void detach()
    {
        mThread.detach();
    }
    
    bool isJoinable()
    {
        return mThread.joinable();
    }

    void join()
    {
        mThread.join();
    }

    bool tryJoin(int32_t milliseconds)
    {
        return mThread.timed_join(boost::posix_time::milliseconds(milliseconds));
    }

    void interrupt()
    {
        mThread.interrupt();
    }

    bool isInterruptionRequested()
    {
        return mThread.interruption_requested();
    }

    static void yield()
    {
        boost::thread::yield();
    }

    static void sleep(int32_t milliseconds)
    {
        boost::thread::sleep(boost::get_system_time() + boost::posix_time::milliseconds(milliseconds));
    }

    static int hardwareConcurrency()
    {
        return boost::thread::hardware_concurrency();
    }

private:
    Thread(const Thread& other);
    Thread& operator=(const Thread& other);

    boost::thread mThread;
};

//
// forward references
//

template <typename T>
class ScopedLock;

template <typename T>
class MutexImpl;

typedef MutexImpl<boost::timed_mutex> FastMutex;

typedef MutexImpl<boost::recursive_timed_mutex> Mutex;

//
// locks
//

template <typename T>
class ScopedLock
{
public:
    typedef T mutex_type;
    
    explicit ScopedLock(mutex_type& mutex)
        : mMutex(mutex)
        , mRawLock(mutex.mRawMutex) { }

    ScopedLock(mutex_type& mutex, int32_t milliseconds)
        : mMutex(mutex)
        , mRawLock(mutex.mRawMutex, boost::chrono::milliseconds(milliseconds))
    {
        if (!mRawLock.owns_lock()) {
            throw TimeoutException();
        }
    }

    void wait()
    {
        mMutex.mRawCond.wait(mRawLock);
    }

    void wait(int32_t milliseconds)
    {
        if (!tryWait(milliseconds)) {
            throw TimeoutException();
        }
    }

    bool tryWait(int32_t milliseconds)
    {
        return mMutex.mRawCond.wait_for(mRawLock, boost::chrono::milliseconds(milliseconds)) == boost::cv_status::no_timeout;
    }

private:
    ScopedLock(const ScopedLock<T>& other);
    ScopedLock<T>& operator=(const ScopedLock<T>& other);
    
    mutex_type& mMutex;
    boost::unique_lock<typename mutex_type::boost_mutex_type> mRawLock;
};

template <typename T>
class ScopedUnlock
{
public:
    typedef T mutex_type;

    explicit ScopedUnlock(mutex_type& mutex)
        : mMutex(mutex)
    {
        mMutex.unlock();
    }

    ~ScopedUnlock()
    {
        mMutex.lock();
    }

private:
    ScopedUnlock(const ScopedUnlock<T>& other);
    ScopedUnlock& operator=(const ScopedUnlock<T>& other);

    mutex_type& mMutex;
};

// #define loo_scopedLock_(M)   ::loo::ScopedLock<std::remove_reference<decltype(M)>::type>  UT_ANONYMOUS_VARIABLE(loo_anonymousScopedLock)(M);
// #define loo_scopedUnlock_(M) ::loo::ScopedUnlock<std::remove_reference<decltype(M)>::type> UT_ANONYMOUS_VARIABLE(loo_anonymousScopedUnlock)(M);

// #define loo_scopedLock_(M)   ::loo::ScopedLock<decltype(M)>  UT_ANONYMOUS_VARIABLE(loo_anonymousScopedLock)(M);
// #define loo_scopedUnlock_(M) ::loo::ScopedUnlock<decltype(M)> UT_ANONYMOUS_VARIABLE(loo_anonymousScopedUnlock)(M);

//
// mutexes
//

template <typename T>
class MutexImpl
{
public:
    MutexImpl() { }

    void lock()
    {
        mRawMutex.lock();
    }

    void lock(int32_t milliseconds)
    {
        if (!tryLock(milliseconds)) {
            throw TimeoutException();
        }
    }

    bool tryLock()
    {
        return mRawMutex.try_lock();
    }

    bool tryLock(int32_t milliseconds)
    {
        return mRawMutex.try_lock_for(boost::chrono::milliseconds(milliseconds));
    }

    void unlock()
    {
        mRawMutex.unlock();
    }

    void signal()
    {
        mRawCond.notify_one();
    }

    void broadcast()
    {
        mRawCond.notify_all();
    }

private:
    typedef T boost_mutex_type;

    MutexImpl(const MutexImpl<T>& other);
    MutexImpl<T>& operator=(const MutexImpl<T>& other);

    boost_mutex_type mRawMutex;
    boost::condition_variable_any mRawCond;

    friend class ScopedLock<MutexImpl<T> >;
};

//
// barrier
//

class Barrier
{
public:
    Barrier(int32_t count)
        : mRawBarrier(count) { }

    bool wait()
    {
        return mRawBarrier.wait();
    }

private:
    boost::barrier mRawBarrier;
};

}
