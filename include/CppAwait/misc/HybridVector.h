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
 * @file  HybridVector.h
 *
 * Declares the HybridVector class.
 *
 */

 #pragma once

#include "../Config.h"
#include <vector>
#include <boost/container/static_vector.hpp>

namespace ut {

/** Vector that switches to dynamic allocation after exceeding some predefined size */
template <typename T, size_t N>
class HybridVector
{
public:
    typedef T value_type;
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;
    typedef value_type& reference;
    typedef const value_type& const_reference;
    typedef value_type* pointer;
    typedef const value_type* const_pointer;
    typedef pointer iterator;
    typedef const_pointer const_iterator;

    HybridVector()
        : mVector(nullptr)
    {
    }

    ~HybridVector()
    {
        delete mVector;
    }

    HybridVector(const HybridVector& other)
        : mArray(other.mArray)
    {
        if (other.mVector) {
            mVector = new std::vector<T>(*other.mVector);
        } else {
            mVector = nullptr;
        }
    }

    HybridVector& operator=(const HybridVector& other)
    {
        if (this != &other) {
            mArray = other.mArray;
            delete mVector;

            if (other.mVector) {
                mVector = new std::vector<T>(*other.mVector);
            } else {
                mVector = nullptr;
            }
        }

        return *this;
    }

    HybridVector(HybridVector&& other)
        : mArray(std::move(other.mArray))
        , mVector(other.mVector)
    {
        other.mVector = nullptr;
    }

    HybridVector& operator=(HybridVector&& other)
    {
        mArray = std::move(other.mArray);
        delete mVector;
        mVector = other.mVector;
        other.mVector = nullptr;

        return *this;
    }

    T& at(size_t pos)
    {
        return mVector ? mVector->at(pos) : mArray.at(pos);
    }

    const T& at(size_t pos) const
    {
        return mVector ? mVector->at(pos) : mArray.at(pos);
    }

    T& operator[](size_t pos)
    {
        return mVector ? (*mVector)[pos] : mArray[pos];
    }

    const T& operator[](size_t pos) const
    {
        return mVector ? (*mVector)[pos] : mArray[pos];
    }

    T& front()
    {
        return mVector ? mVector->front() : mArray.front();
    }

    const T& front() const
    {
        return mVector ? mVector->front() : mArray.front();
    }

    T& back()
    {
        return mVector ? mVector->back() : mArray.back();
    }

    const T& back() const
    {
        return mVector ? mVector->back() : mArray.back();
    }

    T* data()
    {
        return mVector ? mVector->data() : mArray.data();
    }

    const T* data() const
    {
        return mVector ? mVector->data() : mArray.data();
    }

    T* begin()
    {
        return (T*) cbegin();
    };

    const T* begin() const
    {
        return cbegin();
    };

    const T* cbegin() const
    {
        if (mVector) {
            return mVector->empty() ? nullptr : &mVector->front();
        } else {
            return mArray.empty() ? nullptr : &mArray.front();
        }
    };

    T* end()
    {
        return (T*) cend();
    };

    const T* end() const
    {
        return cend();
    };

    const T* cend() const
    {
        if (mVector) {
            return mVector->empty() ? nullptr : &mVector->back() + 1;
        } else {
            return mArray.empty() ? nullptr : &mArray.back() + 1;
        }
    };

    size_t empty() const
    {
        return mVector ? mVector->empty() : mArray.empty();
    }

    size_t size() const
    {
        return mVector ? mVector->size() : mArray.size();
    }

    void reserve(size_t newCapacity)
    {
        if (mVector) {
            mVector->reserve(newCapacity);
        } else {
            if (newCapacity > mArray.capacity()) {
                switchToHeap();
                reserve(newCapacity);
            }
        }
    }

    size_t capacity() const
    {
        return mVector ? mVector->capacity() : mArray.capacity();
    }

    void clear()
    {
        if (mVector) {
            mVector->clear();
        } else {
            mArray.clear();
        }
    }

    void insert(const_iterator pos, const T& value)
    {
        if (mVector) {
            mVector->insert(vPos(pos), value);
        } else {
            if (mArray.size() < mArray.capacity()) {
                mArray.insert(aPos(pos), value);
            } else {
                size_t index = pos - begin();
                switchToHeap();
                insert(begin() + index, value);
            }
        }
    }

    void insert(const_iterator pos, T&& value)
    {
        if (mVector) {
            mVector->insert(vPos(pos), std::move(value));
        } else {
            if (mArray.size() < mArray.capacity()) {
                mArray.insert(aPos(pos), std::move(value));
            } else {
                size_t index = pos - begin();
                switchToHeap();
                insert(begin() + index, std::move(value));
            }
        }
    }

    void insert(const_iterator pos, size_t count, const T& value)
    {
        if (mVector) {
            mVector->insert(vPos(pos), count, value);
        } else {
            if (mArray.size() + count <= mArray.capacity()) {
                mArray.insert(aPos(pos), count, value);
            } else {
                size_t index = pos - begin();
                switchToHeap();
                insert(begin() + index, count, value);
            }
        }
    }

    template <typename InputIt>
    void insert(const_iterator pos, InputIt first, InputIt last)
    {
        if (mVector) {
            mVector->insert(vPos(pos), first, last);
        } else {
            size_t count = (size_t) (last - first);

            if (mArray.size() + count <= mArray.capacity()) {
                mArray.insert(aPos(pos), first, last);
            } else {
                size_t index = pos - begin();
                switchToHeap();
                insert(begin() + index, first, last);
            }
        }
    }

    void erase(const_iterator pos)
    {
        if (mVector) {
            mVector->erase(vPos(pos));
        } else {
            mArray.erase(aPos(pos));
        }
    }

    void erase(const_iterator first, const_iterator last)
    {
        if (mVector) {
            mVector->erase(vPos(first), vPos(last));
        } else {
            mArray.erase(aPos(first), aPos(last));
        }
    }

    void push_back(const T& value)
    {
        if (mVector) {
            mVector->push_back(value);
        } else {
            if (mArray.size() < mArray.capacity()) {
                mArray.push_back(value);
            } else {
                switchToHeap();
                push_back(value);
            }
        }
    }

    void push_back(T&& value)
    {
        if (mVector) {
            mVector->push_back(std::move(value));
        } else {
            if (mArray.size() < mArray.capacity()) {
                mArray.push_back(std::move(value));
            } else {
                switchToHeap();
                push_back(std::move(value));
            }
        }
    }

    void pop_back()
    {
        if (mVector) {
            mVector->pop_back();
        } else {
            mArray.pop_back();
        }
    }

    void resize(size_t count)
    {
        if (mVector) {
            mVector->resize(count);
        } else {
            if (count <= mArray.capacity()) {
                mArray.resize(count);
            } else {
                switchToHeap();
                resize(count);
            }
        }
    }

    void resize(size_t count, const T& value)
    {
        if (mVector) {
            mVector->resize(count, value);
        } else {
            if (count <= mArray.capacity()) {
                mArray.resize(count, value);
            } else {
                switchToHeap();
                resize(count, value);
            }
        }
    }

    void swap(HybridVector& other)
    {
        std::swap(mVector, other.mVector);

        if (!mVector || !other.mVector) {
            mArray.swap(other.mArray);
        }
    }

private:
    auto vPos(const_iterator pos) const
        -> typename std::vector<T>::const_iterator
    {
        return mVector->begin() + (pos - begin());
    }

    // std::vector insert() and erase() take const_iterators
    // since C++11, this overload is for legacy std libraries
    //
    auto vPos(const_iterator pos)
        -> typename std::vector<T>::iterator
    {
        return mVector->begin() + (pos - begin());
    }

    auto aPos(const_iterator pos) const
        -> typename boost::container::static_vector<T, N>::const_iterator
    {
        return mArray.begin() + (pos - begin());
    }

    void switchToHeap()
    {
        mVector = new std::vector<T>();
        mVector->reserve(2 * mArray.capacity());

        for (auto it = mArray.begin(), end = mArray.end(); it != end; ++it) {
            mVector->push_back(std::move(*it));
        }

        mArray.clear();
    }

    boost::container::static_vector<T, N> mArray;
    std::vector<T> *mVector;
};

template <typename T, std::size_t N>
void swap(HybridVector<T, N>& lhs,  HybridVector<T, N>& rhs)
{
    lhs.swap(rhs);
}

}
