#pragma once

#include <atomic>

#include "envoy/common/pure.h"

#include "common/common/assert.h"

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
template <class T> class RefcountPtr {
public:
  RefcountPtr() : ptr_(nullptr) {}
  RefcountPtr(T* ptr) : ptr_(ptr) {
    if (ptr_ != nullptr) {
      ptr_->incRefCount();
    }
  }

  RefcountPtr(const RefcountPtr& src) { set(src.get()); }

  // Allows RefcountPtr<BaseClass> foo = RefcountPtr<DerivedClass>.
  template <class U> RefcountPtr(const RefcountPtr<U>& src) { set(src.get()); }

  // Move-construction is used by absl::flat_hash_map during resizes.
  RefcountPtr(RefcountPtr&& src) noexcept : ptr_(src.ptr_) { src.ptr_ = nullptr; }

  RefcountPtr& operator=(const RefcountPtr& src) {
    if (&src != this && src.ptr_ != ptr_) {
      resetInternal();
      set(src.get());
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
  bool operator!=(const T* ptr) const { return ptr_ != ptr; }
  bool operator==(const RefcountPtr& a) const { return ptr_ == a.ptr_; }
  bool operator!=(const RefcountPtr& a) const { return ptr_ != a.ptr_; }
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

  void set(T* ptr) {
    ptr_ = ptr;
    if (ptr_ != nullptr) {
      ptr_->incRefCount();
    }
  }

  T* ptr_;
};

template <class T> static bool operator==(std::nullptr_t, const RefcountPtr<T>& a) {
  return a == nullptr;
}
template <class T> static bool operator!=(std::nullptr_t, const RefcountPtr<T>& a) {
  return a != nullptr;
}

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
  void incRefCount() { ++ref_count_; }
  bool decRefCount() {
    ASSERT(ref_count_ >= 1);
    return --ref_count_ == 0;
  }
  uint32_t use_count() const { return ref_count_; }

  std::atomic<uint32_t> ref_count_{0};
};

} // namespace Stats
} // namespace Envoy
