#pragma once

#include <atomic>

#include "envoy/common/pure.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Stats {

// Implements a reference-counted pointer to a class, so that its external usage
// model is identical to std::shared_ptr, but the reference count itself is held
// in the class. The class is expected to implement three methods:
//    void incRefCount()
//    bool decRefCount()  -- returns true if the reference count goes to zero.
//    uint32_t use_count()
// It may implement them by delegating to RefcountHelper (see below), or by
// inheriting from RefcountInterface (see below).
//
// TODO(jmarantz): replace this with an absl or std implementation when
// available. See https://github.com/abseil/abseil-cpp/issues/344, issued June
// 26, 2019, and http://wg21.link/p0406, issued 2017. Note that the turnaround
// time for getting an absl API added is measurable in months, and for a std
// API in years.
template <class T> class RefcountPtr {
public:
  RefcountPtr() : ptr_(nullptr) {}

  // Constructing a reference-counted object from a pointer; this is safe to
  // do when the reference-count is held in the object. For example, this code
  // crashes:
  //   {
  //     std::shared_ptr<std::string> a = std::make_shared<std::string>("x");
  //     std::shared_ptr<std::string> b(a.get());
  //   }
  // whereas the analogous code for RefcountPtr works fine.
  RefcountPtr(T* ptr) : ptr_(ptr) {
    if (ptr_ != nullptr) {
      ptr_->incRefCount();
    }
  }

  RefcountPtr(const RefcountPtr& src) : RefcountPtr(src.get()) {}

  // Constructor for up-casting reference-counted pointers. This doesn't change
  // the underlying object; it just upcasts the DerivedClass* in src.ptr_ to a
  // BaseClass* for assignment to this->ptr_. For usage of this to compile,
  // DerivedClass* must be assignable to BaseClass* without explicit casts.
  template <class DerivedClass>
  RefcountPtr(const RefcountPtr<DerivedClass>& src) : RefcountPtr(src.get()) {}

  // Move-construction is used by absl::flat_hash_map during resizes.
  RefcountPtr(RefcountPtr&& src) noexcept : ptr_(src.ptr_) { src.ptr_ = nullptr; }

  RefcountPtr& operator=(const RefcountPtr& src) {
    if (&src != this && src.ptr_ != ptr_) {
      resetInternal();
      ptr_ = src.get();
      if (ptr_ != nullptr) {
        ptr_->incRefCount();
      }
    }
    return *this;
  }

  // Move-assignment is used during std::vector resizes.
  RefcountPtr& operator=(RefcountPtr&& src) noexcept {
    if (&src != this && src.ptr_ != ptr_) {
      resetInternal();
      ptr_ = src.ptr_;
      src.ptr_ = nullptr;
    }
    return *this;
  }

  ~RefcountPtr() { resetInternal(); }

  // Implements required subset of the shared_ptr API;
  // see https://en.cppreference.com/w/cpp/memory/shared_ptr for details.
  T* operator->() const { return ptr_; }
  T* get() const { return ptr_; }
  T& operator*() const { return *ptr_; }
  operator bool() const { return ptr_ != nullptr; }
  bool operator==(const T* ptr) const { return ptr_ == ptr; }
// In C++20, operator!= can be defaulted and returns !(x == y) or !(y == x).
// Defining it prevents the compiler from considering the reversed-operand
// versions of equality comparisons, so leave it to be defaulted.
#if __cplusplus < 202002L
  bool operator!=(const T* ptr) const { return ptr_ != ptr; }
#endif
  bool operator==(const RefcountPtr& a) const { return ptr_ == a.ptr_; }
  bool operator!=(const RefcountPtr& a) const { return ptr_ != a.ptr_; }
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDelete)
  uint32_t use_count() const { return ptr_->use_count(); }
  void reset() {
    resetInternal();
    ptr_ = nullptr;
  }

private:
  // Like reset() but does not bother to clear ptr_, as it is about to be
  // overwritten or destroyed.
  void resetInternal() {
    if (ptr_ != nullptr && ptr_->decRefCount()) {
      delete ptr_;
    }
  }

  T* ptr_;
};

// In C++20, reversed-operand versions of comparison operators are considered
// during overload resolution, so these functions are no longer necessary and
// cause segfaults due to attempting to infinitely reverse the operands back and
// forth.
#if __cplusplus < 202002L
template <class T> static bool operator==(std::nullptr_t, const RefcountPtr<T>& a) {
  return a == nullptr; // NOLINT(clang-analyzer-cplusplus.Move)
}
template <class T> static bool operator!=(std::nullptr_t, const RefcountPtr<T>& a) {
  return a != nullptr;
}
#endif

// Helper interface for classes to derive from, enabling implementation of the
// three methods as part of derived classes. It is not necessary to inherit from
// this interface to wrap a class in RefcountPtr; instead the class can just
// implement the same method.
class RefcountInterface {
public:
  virtual ~RefcountInterface() = default;

  /**
   * Increments the reference count.
   */
  virtual void incRefCount() PURE;

  /**
   * Decrements the reference count.
   * @return true if the reference count has gone to zero, so the object should be freed.
   */
  virtual bool decRefCount() PURE;

  /**
   * @return the number of references to the object.
   */
  virtual uint32_t use_count() const PURE;
};

// Delegation helper for RefcountPtr. This can be instantiated in a class, but
// explicit delegation will be needed for each of the three methods.
struct RefcountHelper {
  // Implements the RefcountInterface API.
  void incRefCount() {
    // Note: The ++ref_count_ here and --ref_count_ below have implicit memory
    // orderings of sequentially consistent. Relaxed on addition and
    // acquire/release on subtraction is the typical use for reference
    // counting. On x86, the difference in instruction count is minimal, but the
    // savings are greater on other platforms.
    //
    // https://www.boost.org/doc/libs/1_69_0/doc/html/atomic/usage_examples.html#boost_atomic.usage_examples.example_reference_counters
    ++ref_count_;
  }
  bool decRefCount() {
    ASSERT(ref_count_ >= 1);
    return --ref_count_ == 0;
  }
  uint32_t use_count() const { return ref_count_; }

  std::atomic<uint32_t> ref_count_{0};
};

} // namespace Stats
} // namespace Envoy
