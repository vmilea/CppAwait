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
 * @file  FastAction.h
 *
 * Declares the FastAction class.
 *
 */

 #pragma once

#include "../Config.h"
#include <type_traits>
#include <stdexcept>
#include <cstddef>
#include <cassert>

namespace ut {

namespace detail
{
    struct TrivialFunctionCaller
    {
        void (*f)();

        void operator()() const
        {
            f();
        }
    };

    template <typename T, typename Method>
    struct TrivialMethodCaller
    {
        T *object;
        Method method;

        void operator()() const
        {
            (object->*method)();
        }
    };
}

template <size_t fast_action_size>
class FastAction;

class FastActionBase
{
public:
    static const size_t in_place_alignment = 8;
    static const size_t overhead_size = (sizeof(void *) == 4 ? 16 : 24);

protected:
    enum Operation
    {
        OP_CLEAR,
        OP_COPY_TO,
        OP_MOVE_TO
    };

    struct stateless_tag { };
    struct in_place_alloc_tag { };
    struct heap_alloc_tag { };
};

/** Helps pick a FastAction that will employ small functor optimization up to max_capture_size */
template <size_t max_capture_size>
struct OptimalAction
{
    static const size_t size = FastActionBase::overhead_size
        + (max_capture_size % 8 == 0
            ? max_capture_size
            : max_capture_size + (8 - max_capture_size % 8));

    typedef FastAction<size> type;
};

/**
 * High performance replacement for nullary std::function
 *
 * FastAction uses metaprogramming to select the fastest possible delegate for
 * functors, methods and free functions. The internal padding can be adjusted
 * for predictable small functor optimization.
 */
template <size_t fast_action_size>
class FastAction : public FastActionBase
{
public:
    static_assert (fast_action_size > overhead_size, "fast_action_size too small");
    static_assert (fast_action_size % in_place_alignment == 0, "fast_action_size must be a multiple of 8");

    static const size_t size = fast_action_size;
    static const size_t in_place_size = size - overhead_size;

    template <typename F>
    struct CanStoreInPlace
    {
        static const bool value = (
            sizeof(F) <= in_place_size &&
            in_place_alignment % std::alignment_of<F>::value == 0);
    };

    FastAction()
        : mTarget(nullptr)
    {
    }

    template <typename F>
    FastAction(F f)
    {
        static_assert (!std::is_reference<F>::value, "invalid F");

        mDelegate = &functorDelegate<F>;
        mManage = &Manager<F, typename FunctorAlloc<F>::tag_type>::manage;

#ifdef _MSC_VER
    __pragma(warning(push))
    __pragma(warning(disable:4127)) // 'conditional expression is constant' for trivial lambdas
#endif
        if (CanStoreInPlace<F>::value) {
            new (&mData) F(std::move(f));
            mTarget = &mData;
        } else {
            mTarget = new F(std::move(f));
        }
#ifdef _MSC_VER
    __pragma(warning(pop))
#endif
    }

    FastAction(void (*f)())
    {
        typedef detail::TrivialFunctionCaller functor_type;

        static_assert (CanStoreInPlace<functor_type>::value, "");

        mDelegate = &functorDelegate<functor_type>;
        mManage = &Manager<functor_type, in_place_alloc_tag>::manage;

        auto functor = new (&mData) functor_type;
        functor->f = f;
        mTarget = &mData;
    }

    template <typename T>
    FastAction(T *object, void (T::*method)())
    {
        typedef detail::TrivialMethodCaller<T, void (T::*)()> functor_type;

        static_assert (CanStoreInPlace<functor_type>::value, "");

        mDelegate = &functorDelegate<functor_type>;
        mManage = &Manager<functor_type, in_place_alloc_tag>::manage;

        auto functor = new (&mData) functor_type;
        functor->object = object;
        functor->method = method;
        mTarget = &mData;
    }

    template <typename T>
    FastAction(const T *object, void (T::*method)() const)
    {
        typedef detail::TrivialMethodCaller<const T, void (T::*)() const> functor_type;

        static_assert (CanStoreInPlace<functor_type>::value, "");

        mDelegate = &functorDelegate<functor_type>;
        mManage = &Manager<functor_type, in_place_alloc_tag>::manage;

        auto functor = new (&mData) functor_type;
        functor->object = object;
        functor->method = method;
        mTarget = &mData;
    }

    template <void (*function)()>
    static FastAction fromFunction0()
    {
        FastAction action;

        action.mTarget = (void *) 1;
        action.mDelegate = &function0Delegate<function>;
        action.mManage = &Manager<void, stateless_tag>::manage;

        return std::move(action);
    }

    template <typename T, void (*function)(T *)>
    static FastAction fromFunction1(T *arg1)
    {
        if (arg1 == nullptr) {
            throw std::invalid_argument("arg1 may not be null");
        }

        FastAction action;

        action.mTarget = arg1;
        action.mDelegate = &function1Delegate<T, function>;
        action.mManage = &Manager<void, stateless_tag>::manage;

        return std::move(action);
    }

    template <typename T, void (T::*method)()>
    static FastAction fromMethod(T *object)
    {
        if (object == nullptr) {
            throw std::invalid_argument("object may not be null");
        }

        FastAction action;

        action.mTarget = object;
        action.mDelegate = &methodDelegate<T, method>;
        action.mManage = &Manager<void, stateless_tag>::manage;

        return std::move(action);
    }

    template <typename T, void (T::*method)() const>
    static FastAction fromConstMethod(const T *object)
    {
        if (object == nullptr) {
            throw std::invalid_argument("object may not be null");
        }

        FastAction action;

        action.mTarget = (void *) object;
        action.mDelegate = &constMethodDelegate<T, method>;
        action.mManage = &Manager<void, stateless_tag>::manage;

        return std::move(action);
    }

    FastAction(const FastAction& other)
        : mTarget(nullptr)
    {
        if (other.mTarget != nullptr) {
            other.mManage(OP_COPY_TO, const_cast<FastAction*>(&other), this);
        }
    }

    FastAction(FastAction&& other)
        : mTarget(nullptr)
    {
        if (other.mTarget != nullptr) {
            other.mManage(OP_MOVE_TO, &other, this);
        }
    }

    FastAction& operator=(const FastAction& other)
    {
        if (other.mTarget != nullptr) {
            other.mManage(OP_COPY_TO, const_cast<FastAction*>(&other), this);
        } else if (mTarget != nullptr) {
            this->mManage(OP_CLEAR, this, nullptr);
        }

        return *this;
    }

    FastAction& operator=(FastAction&& other)
    {
        if (other.mTarget != nullptr) {
            other.mManage(OP_MOVE_TO, const_cast<FastAction*>(&other), this);
        } else if (mTarget != nullptr) {
            this->mManage(OP_CLEAR, this, nullptr);
        }

        return *this;
    }

    FastAction& operator=(void (*f)())
    {
        detail::TrivialFunctionCaller functor;
        functor.f = f;

        *this = std::move(functor);

        return *this;
    }

    template <typename F>
    FastAction& operator=(F f)
    {
        if (mTarget != nullptr && mManage == &Manager<F, typename FunctorAlloc<F>::tag_type>::manage) {
            if (CanStoreInPlace<F>::value) {
                assert (hasInPlaceTarget());

                // can't use assignment with lambdas, move construct instead
                target<F>().~F();
                new (mTarget) F(std::move(f));
            } else {
                assert (!hasInPlaceTarget());

                // can't use assignment with lambdas, move construct instead
                delete targetPtr<F>();
                mTarget = new F(std::move(f));
            }
        } else {
            // slow path
            *this = FastAction(std::move(f));
        }

        return *this;
    }

    FastAction& operator=(std::nullptr_t)
    {
        if (mTarget != nullptr) {
            mManage(OP_CLEAR, this, nullptr);
        }

        return *this;
    }

    ~FastAction()
    {
        if (mTarget != nullptr) {
            mManage(OP_CLEAR, this, nullptr);
        }
    }

    typedef void (FastAction::*bool_type)() const;

    operator bool_type() const
    {
        return (mTarget ? &FastAction::bool_method : 0);
    }

    void operator()() const
    {
        assert (mTarget && "can't call an empty FastAction");

        mDelegate(mTarget);
    }

private:
    void bool_method() const { }

    template <typename F, bool canStoreInPlace>
    struct FunctorAllocImpl
    {
        typedef heap_alloc_tag tag_type;
    };

    template <typename F>
    struct FunctorAllocImpl<F, true>
    {
        typedef in_place_alloc_tag tag_type;
    };

    template <typename F>
    struct FunctorAlloc
        : FunctorAllocImpl<F, CanStoreInPlace<F>::value> { };

    template <typename F, typename AllocTag>
    struct Manager;

    template <typename D>
    struct ManagerBase
    {
        static void manage(Operation op, FastAction *self, FastAction *other)
        {
            assert (self->mTarget != nullptr);

            if (op == OP_CLEAR) {
                D::clear(self);
            } else if (op == OP_MOVE_TO) {
                D::moveTo(self, other);
            } else if (op == OP_COPY_TO) {
                D::copyTo(self, other);
            }
        }
    };

    template <typename F>
    struct Manager<F, stateless_tag> : public ManagerBase<Manager<F, stateless_tag>>
    {
        static void moveTo(FastAction *self, FastAction *other)
        {
            if (other->mTarget != nullptr && self->mManage != other->mManage) {
                other->mManage(OP_CLEAR, other, nullptr);
            }

            other->mTarget = self->mTarget;
            self->mTarget = nullptr;
            other->mDelegate = self->mDelegate;
            other->mManage = self->mManage;
        }

        static void copyTo(FastAction *self, FastAction *other)
        {
            if (self == other) {
                return;
            }

            if (other->mTarget != nullptr && self->mManage != other->mManage) {
                other->mManage(OP_CLEAR, other, nullptr);
            }

            other->mTarget = self->mTarget;
            other->mDelegate = self->mDelegate;
            other->mManage = self->mManage;
        }

        static void clear(FastAction *self)
        {
            self->mTarget = nullptr;
        }
    };

    template <typename F>
    struct Manager<F, in_place_alloc_tag> : public ManagerBase<Manager<F, in_place_alloc_tag>>
    {
        static void moveTo(FastAction *self, FastAction *other)
        {
            assert (self->hasInPlaceTarget());

            if (other->mTarget == nullptr) {
                new (&other->mData) F(std::move(self->target<F>()));
                other->mTarget = &other->mData;
            } else {
                if (self->mManage != other->mManage) {
                    other->mManage(OP_CLEAR, other, nullptr);
                    moveTo(self, other);
                    return;
                }

                assert (other->hasInPlaceTarget());

                // can't use assignment with lambdas, move construct instead
                other->target<F>().~F();
                new (other->mTarget) F(std::move(self->target<F>()));
            }

            self->target<F>().~F();
            self->mTarget = nullptr;

            other->mDelegate = self->mDelegate;
            other->mManage = self->mManage;
        }

        static void copyTo(FastAction *self, FastAction *other)
        {
            assert (self->hasInPlaceTarget());

            if (self == other) {
                return;
            }

            if (other->mTarget == nullptr) {
                new (&other->mData) F(self->target<F>());
                other->mTarget = &other->mData;
            } else {
                if (self->mManage != other->mManage) {
                    other->mManage(OP_CLEAR, other, nullptr);
                    copyTo(self, other);
                    return;
                }

                assert (other->hasInPlaceTarget());

                // can't use assignment with lambdas, copy construct instead
                other->target<F>().~F();
                new (other->mTarget) F(self->target<F>());
            }

            other->mDelegate = self->mDelegate;
            other->mManage = self->mManage;
        }

        static void clear(FastAction *self)
        {
            assert (self->hasInPlaceTarget());

            self->target<F>().~F();
            self->mTarget = nullptr;
        }
    };

    template <typename F>
    struct Manager<F, heap_alloc_tag> : public ManagerBase<Manager<F, heap_alloc_tag>>
    {
        static void moveTo(FastAction *self, FastAction *other)
        {
            assert (!self->hasInPlaceTarget());

            if (other->mTarget != nullptr) {
                if (self->mManage != other->mManage) {
                    other->mManage(OP_CLEAR, other, nullptr);
                    moveTo(self, other);
                    return;
                }

                assert (!other->hasInPlaceTarget());

                delete other->targetPtr<F>();
            }

            other->mTarget = self->mTarget;
            self->mTarget = nullptr;

            other->mDelegate = self->mDelegate;
            other->mManage = self->mManage;
        }

        static void copyTo(FastAction *self, FastAction *other)
        {
            assert (!self->hasInPlaceTarget());

            if (self == other) {
                return;
            }

            if (other->mTarget == nullptr) {
                other->mTarget = new F(self->target<F>());
            } else {
                if (self->mManage != other->mManage) {
                    other->mManage(OP_CLEAR, other, nullptr);
                    copyTo(self, other);
                    return;
                }

                assert (!other->hasInPlaceTarget());

                // can't use assignment with lambdas, copy construct instead
                delete other->targetPtr<F>();
                other->mTarget = new F(self->target<F>());
            }

            other->mDelegate = self->mDelegate;
            other->mManage = self->mManage;
        }

        static void clear(FastAction *self)
        {
            assert (!self->hasInPlaceTarget());

            delete self->targetPtr<F>();
            self->mTarget = nullptr;
        }
    };

    template <typename F>
    F* targetPtr()
    {
        return reinterpret_cast<F*>(mTarget);
    }

    template <typename F>
    F& target()
    {
        return *targetPtr<F>();
    }

    template <void (*function)()>
    static void function0Delegate(void * /* ignore */)
    {
        function();
    }

    template <typename T, void (*function)(T *)>
    static void function1Delegate(void *target)
    {
        function(reinterpret_cast<T*>(target));
    }

    template <typename T, void (T::*method)()>
    static void methodDelegate(void *target)
    {
        (reinterpret_cast<T*>(target)->*method)();
    }

    template <typename T, void (T::*method)() const>
    static void constMethodDelegate(void *target)
    {
        (reinterpret_cast<const T*>(target)->*method)();
    }

    template <typename F>
    static void functorDelegate(void *target)
    {
        reinterpret_cast<F*>(target)->operator()();
    }

    bool hasInPlaceTarget()
    {
        return mTarget == &mData;
    }

    typename std::aligned_storage<in_place_size, in_place_alignment>::type mData;
    void *mTarget;

    void (*mDelegate)(void *target);
    void (*mManage)(Operation op, FastAction *self, FastAction *other);
};

template <size_t fast_action_size, typename T>
bool operator==(const FastAction<fast_action_size>& lhs, const T& rhs)
{
    static_assert (fast_action_size == 0, "may only compare to nullptr");
    return false;
}

template <size_t fast_action_size, typename T>
bool operator!=(const FastAction<fast_action_size>& lhs, const T& rhs)
{
    static_assert (fast_action_size == 0, "may only compare to nullptr");
    return false;
}

template <size_t fast_action_size, typename T>
bool operator==(const T& lhs, const FastAction<fast_action_size>& rhs)
{
    static_assert (fast_action_size == 0, "may only compare to nullptr");
    return false;
}

template <size_t fast_action_size, typename T>
bool operator!=(const T& lhs, const FastAction<fast_action_size>& rhs)
{
    static_assert (fast_action_size == 0, "may only compare to nullptr");
    return false;
}

template <size_t fast_action_size>
bool operator==(const FastAction<fast_action_size>& lhs, std::nullptr_t)
{
    return !lhs;
}

template <size_t fast_action_size>
bool operator!=(const FastAction<fast_action_size>& lhs, std::nullptr_t)
{
    return lhs;
}

template <size_t fast_action_size>
bool operator==(std::nullptr_t, const FastAction<fast_action_size>& lhs)
{
    return !lhs;
}

template <size_t fast_action_size>
bool operator!=(std::nullptr_t, const FastAction<fast_action_size>& lhs)
{
    return lhs;
}

}
