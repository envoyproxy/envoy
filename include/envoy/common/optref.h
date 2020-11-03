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
// Using absl::optional directly you must write t.get()->method() which is a bit
// more awkward.
template <class T> struct OptRef : public absl::optional<std::reference_wrapper<T>> {
  OptRef(T& t) : absl::optional<std::reference_wrapper<T>>(t) {}
  OptRef() {}

  T* operator->() {
    T& ref = **this;
    return &ref;
  }

  const T* operator->() const {
    const T& ref = **this;
    return &ref;
  }
};

} // namespace Envoy
