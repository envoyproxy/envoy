#pragma once

#include <chrono>

#include "common/common/interval_value.h"

#include "absl/types/variant.h"

namespace Envoy {
namespace Event {

/**
 * Describes a minimum timer value that is equal to a scale factor applied to the maximum.
 */
struct ScaledMinimum {
  explicit ScaledMinimum(UnitFloat scale_factor) : scale_factor_(scale_factor) {}
  const UnitFloat scale_factor_;
};

/**
 * Describes a minimum timer value that is an absolute duration.
 */
struct AbsoluteMinimum {
  explicit AbsoluteMinimum(std::chrono::milliseconds value) : value_(value) {}
  const std::chrono::milliseconds value_;
};

/**
 * Class that describes how to compute a minimum timeout given a maximum timeout value. It wraps
 * ScaledMinimum and AbsoluteMinimum and provides a single computeMinimum() method.
 */
class ScaledTimerMinimum : private absl::variant<ScaledMinimum, AbsoluteMinimum> {
public:
  // Use base class constructor.
  using absl::variant<ScaledMinimum, AbsoluteMinimum>::variant;

  // Computes the minimum value for a given maximum timeout. If this object was constructed with a
  // - ScaledMinimum value:
  //     the return value is the scale factor applied to the provided maximum.
  // - AbsoluteMinimum:
  //     the return value is that minimum, and the provided maximum is ignored.
  std::chrono::milliseconds computeMinimum(std::chrono::milliseconds maximum) const {
    struct Visitor {
      explicit Visitor(std::chrono::milliseconds value) : value_(value) {}
      std::chrono::milliseconds operator()(ScaledMinimum scale_factor) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            scale_factor.scale_factor_.value() * value_);
      }
      std::chrono::milliseconds operator()(AbsoluteMinimum absolute_value) {
        return absolute_value.value_;
      }
      const std::chrono::milliseconds value_;
    };
    return absl::visit<Visitor, const absl::variant<ScaledMinimum, AbsoluteMinimum>&>(
        Visitor(maximum), *this);
  }
};

} // namespace Event
} // namespace Envoy
