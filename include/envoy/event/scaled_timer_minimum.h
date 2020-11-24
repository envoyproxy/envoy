#pragma once

#include <chrono>
#include <ostream>

#include "absl/types/variant.h"

namespace Envoy {
namespace Event {

/**
 * Describes a minimum timer value that is equal to a scale factor applied to the maximum.
 */
struct ScaledMinimum {
  explicit constexpr ScaledMinimum(double scale_factor) : scale_factor_(scale_factor) {}
  inline bool operator==(const ScaledMinimum& other) const {
    return other.scale_factor_ == scale_factor_;
  }
  inline friend std::ostream& operator<<(std::ostream& output, const ScaledMinimum& minimum) {
    return output << "ScaledMinimum { scale_factor_ = " << minimum.scale_factor_ << " }";
  }

  const double scale_factor_;
};

/**
 * Describes a minimum timer value that is an absolute duration.
 */
struct AbsoluteMinimum {
  explicit constexpr AbsoluteMinimum(std::chrono::milliseconds value) : value_(value) {}
  inline bool operator==(const AbsoluteMinimum& other) const { return other.value_ == value_; }
  inline friend std::ostream& operator<<(std::ostream& output, const AbsoluteMinimum& minimum) {
    return output << "AbsoluteMinimum { value = " << minimum.value_.count() << "ms }";
  }
  const std::chrono::milliseconds value_;
};

/**
 * Class that describes how to compute a minimum timeout given a maximum timeout value. It wraps
 * ScaledMinimum and AbsoluteMinimum and provides a single computeMinimum() method.
 */
class ScaledTimerMinimum {
public:
  // Forward arguments to impl_'s constructor.
  template <typename T> constexpr ScaledTimerMinimum(T arg) : impl_(arg) {}

  // Computes the minimum value for a given maximum timeout. If this object was constructed with a
  // - ScaledMinimum value:
  //   the return value is the scale factor applied to the provided maximum.
  // - AbsoluteMinimum:
  //   the return value is that minimum, and the provided maximum is ignored.
  std::chrono::milliseconds computeMinimum(std::chrono::milliseconds maximum) const {
    struct Visitor {
      explicit Visitor(std::chrono::milliseconds value) : value_(value) {}
      std::chrono::milliseconds operator()(ScaledMinimum scale_factor) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(scale_factor.scale_factor_ *
                                                                     value_);
      }
      std::chrono::milliseconds operator()(AbsoluteMinimum absolute_value) {
        return absolute_value.value_;
      }
      const std::chrono::milliseconds value_;
    };
    return absl::visit(Visitor(maximum), impl_);
  }

  inline bool operator==(const ScaledTimerMinimum& other) const { return impl_ == other.impl_; }

  inline friend std::ostream& operator<<(std::ostream& output, const ScaledTimerMinimum& minimum) {
    return absl::visit([&](const auto& minimum) -> std::ostream& { return output << minimum; },
                       minimum.impl_);
  }

private:
  absl::variant<ScaledMinimum, AbsoluteMinimum> impl_;
};

} // namespace Event
} // namespace Envoy
