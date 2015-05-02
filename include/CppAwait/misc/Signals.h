/*
* Copyright 2012-2015 Valentin Milea
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
 * @file  Signals.h
 *
 * Declares the Signal class.
 *
 */

#pragma once

#include "../Config.h"
#include "../impl/SharedFlag.h"
#include "../impl/Assert.h"
#include "../impl/Foreach.h"
#include "../misc/Functional.h"
#include "../misc/HybridVector.h"
#include <algorithm>

namespace ut {

template <typename Signature, typename Slot = std::function<Signature>>
class Signal;

/** Allows disconnecting a single slot from signal */
class SignalConnection
{
public:
    /** Construct a dummy connection */
    SignalConnection() { }

    /** Copy constructor */
    SignalConnection(const SignalConnection& other)
        : mDisconnect(other.mDisconnect) { }

    /** Copy assignment */
    SignalConnection& operator=(const SignalConnection& other)
    {
        if (this != &other) {
            mDisconnect = other.mDisconnect;
        }

        return *this;
    }

    /** Move constructor */
    SignalConnection(SignalConnection&& other)
        : mDisconnect(std::move(other.mDisconnect)) { }

    /** Move assignment */
    SignalConnection& operator=(SignalConnection&& other)
    {
        mDisconnect = std::move(other.mDisconnect);

        return *this;
    }

    /**
     * Disconnect associated slot
     *
     * You may disconnect slots while the signal is being emitted.
     */
    void disconnect()
    {
        if (mDisconnect) {
            mDisconnect();
            mDisconnect = nullptr;
        }
    }

private:
    SignalConnection(ut::Action&& disconnect)
        : mDisconnect(std::move(disconnect)) { }

    ut::Action mDisconnect;

    template <typename Signature, typename Slot>
    friend class Signal;
};

/** Lightweight signal - single threaded, no MPL */
template <typename Signature, typename Slot>
class Signal
{
public:
    /** Slot signature */
    typedef Slot slot_type;

    /** Signal alias */
    typedef Signal<Signature, Slot> signal_type;

    /**
     * Connect a slot
     *
     * You may connect slots while the signal is being emitted.
     *
     * @param   slot     slot to connect
     * @return  a connection object that may be used to disconnect slot
     */
    SignalConnection connect(slot_type slot)
    {
        auto disconnectFlag = allocateSharedFlag(this);

        if (mIsEmitting) {
            mHooksToAdd.push_back(Hook(std::move(slot), disconnectFlag));
        } else {
            mHooks.push_back(Hook(std::move(slot), disconnectFlag));
        }

        return SignalConnection([disconnectFlag]() {
            auto thiz = (signal_type *) *disconnectFlag;

            if (thiz) {
                *disconnectFlag = nullptr;

                thiz->mNumCanceled++;
                thiz->trimCanceled();
            }
        });
    }

    /**
     * Connect a slot
     *
     * This version is slightly faster, but the slot can't be disconnected.
     *
     * @param   slot     slot to connect
     */
    void connectLite(slot_type slot)
    {
        if (mIsEmitting) {
            mHooksToAdd.push_back(Hook(std::move(slot)));
        } else {
            mHooks.push_back(Hook(std::move(slot)));
        }
    }

    /**
     * Disconnect all slots
     *
     * You may disconnect slots while the signal is being emitted.
     */
    void disconnectAll()
    {
        ut_foreach_(auto& hook, mHooksToAdd) {
            hook.cancel();
        }
        ut_foreach_(auto& hook, mHooks) {
            hook.cancel();
        }

        trimCanceled();
    }

protected:
    /** Abstract class */
    Signal()
        : mNumCanceled(0)
        , mIsEmitting(nullptr)
    {
    }

    ~Signal()
    {
        if (!mHooksToAdd.empty()) {
            ut_foreach_(auto& hook, mHooksToAdd) {
                hook.cancel();
            }
        }
        ut_foreach_(auto& hook, mHooks) {
            hook.cancel();
        }

        if (mIsEmitting) {
            // break out of emit loop
            *mIsEmitting = false;
        }
    }

    template <typename F>
    void emitSignal(F&& caller)
    {
        ut_assert_(!mIsEmitting && "may not emit signal from a slot");
        ut_assert_(mNumCanceled == 0);
        ut_assert_(mHooksToAdd.empty());

        size_t n = mHooks.size();
        if (n == 0) {
            return;
        }

        bool isEmitting = true;
        mIsEmitting = &isEmitting;

        for (size_t i = 0; i < n; i++) {
            const Hook& hook = mHooks[i];

            if (mNumCanceled == 0 || !hook.isCanceled()) {
                try {
                    caller(hook.slot());

                    if (!isEmitting) {
                        return;
                    }
                } catch (...) {
                    if (isEmitting) {
                        mIsEmitting = nullptr;
                        trimCanceled();
                    }
                    throw;
                }
            }
        }
        ut_assert_(n == mHooks.size());

        if (!mHooksToAdd.empty()) {
            ut_foreach_(auto& hook, mHooksToAdd) {
                mHooks.push_back(std::move(hook));
            }
            mHooksToAdd.clear();
        }

        mIsEmitting = nullptr;
        trimCanceled();
    }

private:
    Signal(const Signal&); // noncopyable
    Signal& operator=(const Signal&); // noncopyable

    void trimCanceled()
    {
        if (mNumCanceled == 0 || mIsEmitting) {
            return;
        }

        if (mNumCanceled == mHooks.size()) {
#ifndef NDEBUG
            ut_foreach_(auto& hook, mHooks) {
                ut_assert_(hook.isCanceled());
            }
#endif
            mHooks.clear();
        } else {
            auto pos = std::remove_if(mHooks.begin(), mHooks.end(),
                [](const Hook& hook) {
                    return hook.isCanceled();
            });

            ut_assert_((size_t) (mHooks.end() - pos) == mNumCanceled);

            mHooks.erase(pos, mHooks.end());
        }

        mNumCanceled = 0;
    }

    struct Hook
    {
        Hook(slot_type&& slot, const std::shared_ptr<void *>& disconnectFlag)
            : mSlot(std::move(slot))
            , mDisconnectFlag(disconnectFlag)
            , mIsCanceled(false) { }

        Hook(slot_type&& slot)
            : mSlot(std::move(slot))
            , mIsCanceled(false) { }

        Hook(Hook&& other)
            : mSlot(std::move(other.mSlot))
            , mDisconnectFlag(std::move(other.mDisconnectFlag))
            , mIsCanceled(other.mIsCanceled)
        {
            other.mIsCanceled = true;
        }

        Hook& operator=(Hook&& other)
        {
            mSlot = std::move(other.mSlot);
            mDisconnectFlag = std::move(other.mDisconnectFlag);
            mIsCanceled = other.mIsCanceled;
            other.mIsCanceled = true;

            return *this;
        }

        const slot_type& slot() const
        {
            return mSlot;
        }

        bool isCanceled() const
        {
            if (mDisconnectFlag && *mDisconnectFlag == nullptr) {
                mDisconnectFlag.reset();
                mIsCanceled = true;
            }

            return mIsCanceled;
        }

        void cancel()
        {
            if (mDisconnectFlag) {
                *mDisconnectFlag = nullptr;
                mDisconnectFlag.reset();
            }

            mIsCanceled = true;
        }

    private:
        Hook(const Hook& other); // noncopyable
        Hook& operator=(const Hook& other); // noncopyable

        slot_type mSlot;
        mutable std::shared_ptr<void *> mDisconnectFlag;
        mutable bool mIsCanceled;
    };

    std::vector<Hook> mHooksToAdd;
    HybridVector<Hook, 2> mHooks;

    size_t mNumCanceled;
    bool *mIsEmitting;
};

/** Signal with 0 arguments */
class Signal0 : public Signal<void (), ut::Action>
{
public:
    Signal0() { }

    void operator()()
    {
        typedef Signal0::slot_type slot_type;

        this->emitSignal([](const slot_type& slot) {
            slot();
        });
    }

private:
    Signal0(const Signal0&); // noncopyable
    Signal0& operator=(const Signal0&); // noncopyable
};

/** Signal with 1 argument */
template <typename Arg1>
class Signal1 : public Signal<void (const Arg1&)>
{
public:
    Signal1() { }

    void operator()(const Arg1& arg1)
    {
        typedef typename Signal1<Arg1>::slot_type slot_type;

        this->emitSignal([&](const slot_type& slot) {
            slot(arg1);
        });
    }

private:
    Signal1(const Signal1&); // noncopyable
    Signal1& operator=(const Signal1&); // noncopyable
};

/** Signal with 2 arguments */
template <typename Arg1, typename Arg2>
class Signal2 : public Signal<void (const Arg1&, const Arg2&)>
{
public:
    Signal2() { }

    void operator()(const Arg1& arg1, const Arg2& arg2)
    {
        typedef typename Signal2<Arg1, Arg2>::slot_type slot_type;

        this->emitSignal([&](const slot_type& slot) {
            slot(arg1, arg2);
        });
    }

private:
    Signal2(const Signal2&); // noncopyable
    Signal2& operator=(const Signal2&); // noncopyable
};

/** Signal with 3 arguments */
template <typename Arg1, typename Arg2, typename Arg3>
class Signal3 : public Signal<void (const Arg1&, const Arg2&, const Arg3&)>
{
public:
    Signal3() { }

    void operator()(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3)
    {
        typedef typename Signal3<Arg1, Arg2, Arg3>::slot_type slot_type;

        this->emitSignal([&](const slot_type& slot) {
            slot(arg1, arg2, arg3);
        });
    }

private:
    Signal3(const Signal3&); // noncopyable
    Signal3& operator=(const Signal3&); // noncopyable
};

/** Signal with 4 arguments */
template <typename Arg1, typename Arg2, typename Arg3, typename Arg4>
class Signal4 : public Signal<void (const Arg1&, const Arg2&, const Arg3&, const Arg4&)>
{
public:
    Signal4() { }

    void operator()(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3, const Arg4& arg4)
    {
        typedef typename Signal4<Arg1, Arg2, Arg3, Arg4>::slot_type slot_type;

        this->emitSignal([&](const slot_type& slot) {
            slot(arg1, arg2, arg3, arg4);
        });
    }

private:
    Signal4(const Signal4&); // noncopyable
    Signal4& operator=(const Signal4&); // noncopyable
};

/** Signal with 5 arguments */
template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5>
class Signal5 : public Signal<void (const Arg1&, const Arg2&, const Arg3&, const Arg4&, const Arg5&)>
{
public:
    Signal5() { }

    void operator()(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3, const Arg4& arg4, const Arg5& arg5)
    {
        typedef typename Signal5<Arg1, Arg2, Arg3, Arg4, Arg5>::slot_type slot_type;

        this->emitSignal([&](const slot_type& slot) {
            slot(arg1, arg2, arg3, arg4, arg5);
        });
    }

private:
    Signal5(const Signal5&); // noncopyable
    Signal5& operator=(const Signal5&); // noncopyable
};

}
