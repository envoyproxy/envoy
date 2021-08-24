#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <memory>

#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

/** The implementation of reference counted object is merely wrapping
 * std::shared_ptr. So QuicReferenceCountedImpl class does not do anything
 * related to reference counting as shared_ptr already takes care of that. But
 * it customizes destruction to provide a interface for shared_ptr to destroy
 * the object, because according to the API declared in QuicReferenceCounted,
 * this class has to hide its destructor.
 */
class QuicReferenceCountedImpl {
public:
  QuicReferenceCountedImpl() = default;

  // Expose destructor through this method.
  static void destroy(QuicReferenceCountedImpl* impl) { delete impl; }

protected:
  // Non-public destructor to match API declared in QuicReferenceCounted.
  virtual ~QuicReferenceCountedImpl() = default;
};

template <typename T> class QuicReferenceCountedPointerImpl {
public:
  QuicReferenceCountedPointerImpl() : refptr_(nullptr, T::destroy) {}
  QuicReferenceCountedPointerImpl(T* p) : refptr_(p, T::destroy) {}
  QuicReferenceCountedPointerImpl(std::nullptr_t) : refptr_(nullptr, T::destroy) {}
  // Copy constructor.
  template <typename U>
  QuicReferenceCountedPointerImpl(const QuicReferenceCountedPointerImpl<U>& other)
      : refptr_(other.refptr()) {}
  QuicReferenceCountedPointerImpl(const QuicReferenceCountedPointerImpl& other)
      : refptr_(other.refptr()) {}

  // Move constructor.
  template <typename U>
  QuicReferenceCountedPointerImpl(QuicReferenceCountedPointerImpl<U>&& other) noexcept
      : refptr_(std::move(other.refptr())) {}
  QuicReferenceCountedPointerImpl(QuicReferenceCountedPointerImpl&& other) noexcept
      : refptr_(std::move(other.refptr())) {}

  ~QuicReferenceCountedPointerImpl() = default;

  // Copy assignments.
  QuicReferenceCountedPointerImpl& operator=(const QuicReferenceCountedPointerImpl& other) {
    refptr_ = other.refptr();
    return *this;
  }
  template <typename U>
  QuicReferenceCountedPointerImpl& operator=(const QuicReferenceCountedPointerImpl<U>& other) {
    refptr_ = other.refptr();
    return *this;
  }

  // Move assignments.
  QuicReferenceCountedPointerImpl& operator=(QuicReferenceCountedPointerImpl&& other) noexcept {
    refptr_ = std::move(other.refptr());
    return *this;
  }
  template <typename U>
  QuicReferenceCountedPointerImpl& operator=(QuicReferenceCountedPointerImpl<U>&& other) noexcept {
    refptr_ = std::move(other.refptr());
    return *this;
  }

  QuicReferenceCountedPointerImpl<T>& operator=(T* p) {
    refptr_.reset(p, T::destroy);
    return *this;
  }

  // Returns the raw pointer with no change in reference.
  T* get() const { return refptr_.get(); }

  // Accessors for the referenced object.
  // operator* and operator-> will assert() if there is no current object.
  T& operator*() const {
    assert(refptr_ != nullptr);
    return *refptr_;
  }
  T* operator->() const {
    assert(refptr_ != nullptr);
    return refptr_.get();
  }

  explicit operator bool() const { return static_cast<bool>(refptr_); }

  const std::shared_ptr<T>& refptr() const { return refptr_; }

  std::shared_ptr<T>& refptr() { return refptr_; }

private:
  std::shared_ptr<T> refptr_;
};

} // namespace quic
