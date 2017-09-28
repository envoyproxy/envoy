#pragma once

#include "envoy/common/exception.h"

namespace Envoy {

/**
 * Contains an optional value. Like boost::optional and std::optional (not included in C++11).
 * TODO: Replace with https://github.com/abseil/abseil-cpp/blob/master/absl/types/optional.h
 */
template <typename T> class Optional {
public:
  Optional() {}
  Optional(const T& value) : value_(value), valid_(true) {}

  const T& operator=(const T& new_value) {
    value_ = new_value;
    valid_ = true;
    return value_;
  }

  bool operator==(const Optional<T>& rhs) const {
    if (valid_) {
      return valid_ == rhs.valid_ && value_ == rhs.value_;
    } else {
      return valid_ == rhs.valid_;
    }
  }

  /**
   * @return whether the contained value is valid.
   */
  bool valid() const { return valid_; }

  /**
   * Set the contained value which will make it valid.
   */
  void value(const T& new_value) {
    value_ = new_value;
    valid_ = true;
  }

  /**
   * @return the contained value. Will throw if the contained value is not valid.
   */
  const T& value() const {
    if (!valid_) {
      throw EnvoyException("fetching invalid Optional value");
    }

    return value_;
  }

  /**
   * @return the contained value. Will throw if the contained value is not valid.
   */
  T& value() {
    if (!valid_) {
      throw EnvoyException("fetching invalid Optional value");
    }

    return value_;
  }

private:
  T value_{};
  bool valid_{};
};

} // namespace Envoy
