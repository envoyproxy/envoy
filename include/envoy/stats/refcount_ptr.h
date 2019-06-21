#pragma once

#include <cassert>
#include <atomic>

namespace Envoy {
namespace Stats {

class RefcountInterface {
 public:
  virtual ~RefcountInterface() = default;
  virtual void incRefCount() PURE;
  virtual bool decRefCount() PURE;
  virtual uint32_t use_count() const PURE;
};

struct RefcountHelper {
  //static constexpr uint32_t Unmanaged = 0xffffffff;

  /*
    void assignRefCount() {
    incRefCount();
    ref_count_ += 2;
    assert(ref_count_ == (Unmanaged + 2));
    }
  */

  void incRefCount() {
    //assert(ref_count_ > 0);
    ++ref_count_;
  }

  bool decRefCount() {
    assert(ref_count_ > 0);
    --ref_count_;
    return ref_count_ == 0;
  }

  uint32_t use_count() const { return ref_count_; }

  std::atomic<uint32_t> ref_count_{0 /*Unmanaged*/};
};

/*
class RefcountImpl : public RefcountInterface {
 public:
  void incRefCount() override { helper_.incRefCount(); }
  bool decRefCount() override { return helper_.decRefCount(); }
  uint32_t use_count() const override { return helper_.use_count(); }

 private:
  RefcountHelper helper_;
};
*/

// Alternate implementation of shared_ptr, where the class itself
// contains the reference-count, and supplies incRefCount() call to
// increase, and free() to destruct.
template<class T> class RefcountPtr {
public:
  RefcountPtr() : ptr_(nullptr) {}
  RefcountPtr(T* ptr) : ptr_(ptr) {
    if (ptr_ != nullptr) {
      ptr_->incRefCount();
    }
  }

  RefcountPtr(const RefcountPtr& src) {
    set(src.get());
  }

  template <class U> RefcountPtr(const RefcountPtr<U>& src) {
    set(src.get());
  }

  RefcountPtr(RefcountPtr&& src) : ptr_(src.ptr_) {
    src.ptr_ = nullptr;
  }

  //template<class U> RefcountPtr& operator=(const RefcountPtr<U>& src) {
  RefcountPtr& operator=(const RefcountPtr& src) {
    if (&src != this && src.ptr_ != ptr_) {
      resetInternal();
      set(src.get());
    }
    return *this;
  }

  RefcountPtr& operator=(RefcountPtr&& src) {
    if (&src != this && src.ptr_ != ptr_) {
      resetInternal();
      ptr_ = src.ptr_;
      src.ptr_ = nullptr;
    }
    return *this;
  }

  /*
  RefcountPtr& operator=(T* ptr) {
    if (ptr_ != ptr) {
      resetInternal();
      set(ptr);
    }
    return *this;
  }
  */

  ~RefcountPtr() { resetInternal(); }

  T* operator->() const { return ptr_; }
  T* get() const { return ptr_; }
  T& operator*() const { return *ptr_; }
  operator T*() const { return ptr_; }
  uint32_t use_count() const { return ptr_->use_count(); }

  void reset() {
    resetInternal();
    ptr_ = nullptr;
  }

private:
  // like reset() but does not bother to clear ptr_, as it is about to be
  // set or destroyed.
  void resetInternal() {
    if ((ptr_ != nullptr) && ptr_->decRefCount()) {
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

} // namespace Stats
} // namespace Envoy
