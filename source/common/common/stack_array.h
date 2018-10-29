#pragma once

#if !defined(WIN32)
#include <alloca.h>

#else
#include <malloc.h>
#endif

#include <stddef.h>

#include "common/common/assert.h"

namespace Envoy {

// This macro is intended to be used as a replacement for variable-length arrays.
// Note that the StackArray wrapper object will be destructed and each element's
// destructor will be called when it leaves scope. However, the memory containing
// the array won't be deallocated until the function containing the macro returns.
// We can't call alloca in the StackArray constructor since the memory would
// be freed when the constructor returns.
#define STACK_ARRAY(var, type, num) StackArray<type> var(::alloca(sizeof(type) * num), num)

template <typename T> class StackArray {
public:
  StackArray(void* buf, size_t num_items) {
    ASSERT(buf != nullptr, "StackArray received null pointer");
    begin_ = static_cast<T*>(buf);
    end_ = static_cast<T*>(buf) + num_items;
    for (T& ref : *this) {
      new (&ref) T;
    }
  }

  ~StackArray() {
    for (T& ref : *this) {
      ref.~T();
    }
  }

  T* begin() { return begin_; }

  T* end() { return end_; }

  T& operator[](size_t idx) { return begin_[idx]; }

  void* operator new(size_t) = delete;

  void* operator new[](size_t) = delete;

private:
  T* begin_;
  T* end_;
};

} // namespace Envoy
