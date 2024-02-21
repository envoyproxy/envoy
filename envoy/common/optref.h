#pragma once

#include "absl/types/optional.h" // required for absl::nullopt

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
//
// This class also consumes less memory -- e.g. 8 bytes for a pointer rather
// than 16 bytes for a pointer plus a bool with alignment padding.
template <class T> struct OptRef {
  OptRef(T& t) : ptr_(&t) {}
  OptRef() : ptr_(nullptr) {}
  OptRef(absl::nullopt_t) : ptr_(nullptr) {}

  /**
   * Copy constructor that allows conversion.
   */
  template <class From> explicit OptRef(OptRef<From> rhs) : ptr_(rhs.ptr()) {}

  /**
   * Assignment that allows conversion.
   */
  template <class From> OptRef<T>& operator=(OptRef<From> rhs) {
    ptr_ = rhs.ptr();
    return *this;
  }

  /**
   * Cast operator to extract a ref to U from this, assuming T*
   * is assignable to U*. For example, if U* is const T, or t is derived
   * from U.
   *
   * @return a ref to the converted object.
   */
  template <class U> operator OptRef<U>() {
    return ptr_ == nullptr ? absl::nullopt : OptRef<U>(*ptr_);
  }

  /**
   * Helper to call a const method on T. The caller is responsible for ensuring
   * has_value() is true.
   */
  T* operator->() const { return ptr_; }

  /**
   * Helper to convert a OptRef into a pointer. If the optional is not set, returns a nullptr.
   */
  T* ptr() const { return ptr_; }

  /**
   * Helper to convert a OptRef into a ref. Behavior if !has_value() is undefined.
   */
  T& ref() const { return *ptr_; } // NOLINT(clang-analyzer-core.uninitialized.UndefReturn)

  /**
   * Helper to dereference an OptRef. Behavior if !has_value() is undefined.
   */
  T& operator*() const { return *ptr_; } // NOLINT(clang-analyzer-core.uninitialized.UndefReturn)

  /**
   * @return true if the object has a value.
   */
  bool has_value() const { return ptr_ != nullptr; }

  /**
   * @return true if the object has a value.
   */
  bool operator!() const { return ptr_ == nullptr; }
  operator bool() const { return ptr_ != nullptr; }

  /**
   * Copies the OptRef into an optional<T>. To use this, T must supply
   * an assignment operator.
   *
   * It is OK to copy an unset object -- it will result in an optional where
   * has_value() is false.
   *
   * @return an optional copy of the referenced object (or nullopt).
   */
  absl::optional<T> copy() const {
    absl::optional<T> ret;
    if (has_value()) {
      ret = *ptr_;
    }
    return ret;
  }

  /**
   * Places a reference to the an object into the OptRef.
   *
   * @param ref the object to be referenced.
   */
  void emplace(T& ref) { ptr_ = &ref; }

  /**
   * The value method has no intrinsic value to OptRef, but is left here for
   * compatibility reasons. This is used in call-sites which would be needed if
   * they were using optional<reference_wrapper<T>> directly without
   * OptRef. Having this function makes it easier upgrade to using OptRef
   * without having to change all call-sites.
   *
   * This must be called with has_value() true.
   *
   * @return a reference_wrapper around the value.
   */
  std::reference_wrapper<const T> value() const { return std::reference_wrapper<T>(*ptr_); }
  std::reference_wrapper<T> value() { return std::reference_wrapper<T>(*ptr_); }

  /**
   * Clears any current reference.
   */
  void reset() { ptr_ = nullptr; }

private:
  T* ptr_;
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

// Overloads for comparing OptRef against absl::nullopt.
template <class T> bool operator!=(const OptRef<T>& optref, absl::nullopt_t) {
  return optref.has_value();
}
template <class T> bool operator!=(absl::nullopt_t, const OptRef<T>& optref) {
  return optref.has_value();
}
template <class T> bool operator==(const OptRef<T>& optref, absl::nullopt_t) {
  return !optref.has_value();
}
template <class T> bool operator==(absl::nullopt_t, const OptRef<T>& optref) {
  return !optref.has_value();
}

} // namespace Envoy
