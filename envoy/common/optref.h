#pragma once

#include "absl/types/optional.h"

namespace Envoy {

// Helper class to make it easier to work with optional references, allowing:
//   foo(OptRef<T> t) {
//     if (t.has_value()) {
//       t->method();
//     }
//   }
//
// Using absl::optional directly you must write optref.value().method() which is
// a bit more awkward.
template <class T> struct OptRef : public absl::optional<std::reference_wrapper<T>> {
  OptRef(T& t) : absl::optional<std::reference_wrapper<T>>(t) {}
  OptRef() = default;

  /**
   * Copy constructor that allows conversion.
   */
  template <class From> explicit OptRef(OptRef<From> rhs) {
    if (rhs.has_value()) {
      *this = rhs.ref();
    }
  }

  /**
   * Assignment that allows conversion.
   */
  template <class From> OptRef<T>& operator=(OptRef<From> rhs) {
    this->reset();
    if (rhs.has_value()) {
      *this = rhs.ref();
    }
    return *this;
  }

  /**
   * Helper to call a method on T. The caller is responsible for ensuring
   * has_value() is true.
   */
  T* operator->() {
    T& ref = **this;
    return &ref;
  }

  /**
   * Helper to call a const method on T. The caller is responsible for ensuring
   * has_value() is true.
   */
  const T* operator->() const {
    const T& ref = **this;
    return &ref;
  }

  /**
   * Helper to convert a OptRef into a pointer. If the optional is not set, returns a nullptr.
   */
  T* ptr() {
    if (this->has_value()) {
      T& ref = **this;
      return &ref;
    }

    return nullptr;
  }

  /**
   * Helper to convert a OptRef into a pointer. If the optional is not set, returns a nullptr.
   */
  const T* ptr() const {
    if (this->has_value()) {
      const T& ref = **this;
      return &ref;
    }

    return nullptr;
  }

  T& ref() { return **this; }

  const T& ref() const { return **this; }
};

/**
 * Constructs an OptRef<T> from the provided reference.
 * @param ref the reference to wrap
 * @return OptRef<T> the wrapped reference
 */
template <class T> OptRef<T> makeOptRef(T& ref) { return {ref}; }

/**
 * Constructs an OptRef<T> from the provided pointer.
 * @param ptr the pointer to wrap
 * @return OptRef<T> the wrapped pointer, or absl::nullopt if the pointer is nullptr
 */
template <class T> OptRef<T> makeOptRefFromPtr(T* ptr) {
  if (ptr == nullptr) {
    return {};
  }

  return {*ptr};
}

} // namespace Envoy
