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
// Note that the StackArray wrapper object will be destructed when it leaves
// scope, however the memory containing the array won't be deallocated until
// the function containing the macro returns
#define STACK_ARRAY(var, type, num) StackArray<type> var(::alloca(sizeof(type) * num), num)

template <typename T> class StackArray {
public:
  StackArray(void* buf, size_t num_items) : num_items_(num_items) {
    RELEASE_ASSERT(buf != nullptr, "StackArray received null pointer");
    buf_ = static_cast<T*>(buf);
    for (size_t i = 0; i < num_items_; i++) {
      new (buf_ + i) T;
    }
  }

  ~StackArray() {
    for (size_t i = 0; i < num_items_; i++) {
      buf_[i].~T();
    }
  }

  T* begin() { return buf_; }

  T* end() { return buf_ + num_items_; }

  T& operator[](size_t idx) { return buf_[idx]; }

  void* operator new(size_t) = delete;

  void* operator new[](size_t) = delete;

private:
  T* buf_;
  size_t num_items_;
};

} // namespace Envoy
