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

template <class T> OptRef<T> makeOptRef(T& ref) { return {ref}; }

template <class T> OptRef<T> makeOptRefFromPtr(T* ptr) {
  if (ptr == nullptr) {
    return {};
  }

  return {*ptr};
}

} // namespace Envoy
