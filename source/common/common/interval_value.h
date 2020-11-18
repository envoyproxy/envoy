#pragma once

#include <algorithm>

namespace Envoy {

// Template helper type that represents a closed interval with the given minimum and maximum values.
template <typename T, T MinValue, T MaxValue> struct Interval {
  static_assert(MinValue <= MaxValue, "min must be <= max");
  static constexpr T min_value = MinValue;
  static constexpr T max_value = MaxValue;
};

// Utility type that represents a value of type T in the given interval.
template <typename T, typename Interval> class IntervalValue {
public:
  static IntervalValue min() { return IntervalValue(Interval::min_value); }
  static IntervalValue max() { return IntervalValue(Interval::max_value); }

  constexpr explicit IntervalValue(T value)
      : value_(std::max<T>(Interval::min_value, std::min<T>(Interval::max_value, value))) {}

  T value() const { return value_; }

private:
  T value_;
};

using UnitFloat = IntervalValue<float, Interval<int, 0, 1>>;

} // namespace Envoy
