/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LOCAL_ARRAY_H_included
#define LOCAL_ARRAY_H_included

#include <cstddef>
#include <new>

/**
 * A fixed-size array with a size hint. That number of bytes will be allocated
 * on the stack, and used if possible, but if more bytes are requested at
 * construction time, a buffer will be allocated on the heap (and deallocated
 * by the destructor).
 *
 * The API is intended to be a compatible subset of C++0x's std::array.
 */
template <size_t STACK_BYTE_COUNT>
class LocalArray {
public:
    /**
     * Allocates a new fixed-size array of the given size. If this size is
     * less than or equal to the template parameter STACK_BYTE_COUNT, an
     * internal on-stack buffer will be used. Otherwise a heap buffer will
     * be allocated.
     */
    LocalArray(size_t desiredByteCount) : mSize(desiredByteCount) {
        if (desiredByteCount > STACK_BYTE_COUNT) {
            mPtr = new char[mSize];
        } else {
            mPtr = &mOnStackBuffer[0];
        }
    }

    /**
     * Frees the heap-allocated buffer, if there was one.
     */
    ~LocalArray() {
        if (mPtr != &mOnStackBuffer[0]) {
            delete[] mPtr;
        }
    }

    // Capacity.
    size_t size() { return mSize; }
    bool empty() { return mSize == 0; }

    // Element access.
    char& operator[](size_t n) { return mPtr[n]; }
    const char& operator[](size_t n) const { return mPtr[n]; }

private:
    char mOnStackBuffer[STACK_BYTE_COUNT];
    char* mPtr;
    size_t mSize;

    // Disallow copy and assignment.
    LocalArray(const LocalArray&);
    void operator=(const LocalArray&);
};

#endif // LOCAL_ARRAY_H_included
