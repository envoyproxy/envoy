#pragma once

#include <algorithm>
#include <chrono>
#include <functional>

#include "common/access_log/access_log_formatter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrencyLimit {
namespace Common {

/**
 * Mesurement keeps track of a single value based on a comparator function.
 */
template <typename T> class Measurement {
public:
  Measurement<T>(std::function<bool(const T&, const T&)> comparator)
      : Measurement<T>(comparator, T()) {}
  Measurement<T>(std::function<bool(const T&, const T&)> comparator, T initial_value)
      : value_(initial_value), comparator_(comparator) {}

  /**
   * set changes the value is the current value of the Measurement is the zero-value of type T
   * or if comparator between the param and the current value is true.
   * @param value is the value to compare.
   */
  void set(T value) {
    if (value_ == T() || comparator_(value, value_)) {
      value_ = value;
    }
  }

  /**
   * get returns the current value of the Measurement.
   * @return T the current value of the Measurement.
   */
  T get() const { return value_; }

  /**
   * clear sets the value of the Measurement to the zero-value of type T.
   */
  void clear() { value_ = T(); }

private:
  T value_;
  std::function<bool(const T&, const T&)> comparator_;
};

/**
 * MinimumMeasurement is a measurement that uses a less than comparator.
 */
template <typename T> class MinimumMeasurement : public Measurement<T> {
public:
  MinimumMeasurement() : MinimumMeasurement<T>(T()) {}
  MinimumMeasurement(T initial_value)
      : Measurement<T>([](const T& a, const T& b) -> bool { return a < b; }, initial_value) {}
};

/**
 * MaximimMeasurement is a measurement that uses a greater than comparator.
 */
template <typename T> class MaximumMeasurement : public Measurement<T> {
public:
  MaximumMeasurement() : MaximumMeasurement<T>(T()) {}
  MaximumMeasurement(T initial_value)
      : Measurement<T>([](const T& a, const T& b) -> bool { return a > b; }, initial_value) {}
};

/**
 * SampleWindow collects latency metrics based on latency samples.
 */
class SampleWindow {
public:
  SampleWindow();

  /**
   * addSample adds the Round Trip Time (RTT) of a sucessful request.
   * @param rtt the rtt latency of the request.
   * @param inflight_requests the number of concurrent requests on the upstream
   *        when this sample completed
   */
  void addSample(std::chrono::nanoseconds rtt, uint32_t inflight_requests);

  /**
   * addDroppedSample adds a request that failed to get a successful response from the upstream.
   * @param inflight_requests the number of concurrent requests on the upstream
   *        when this sample completed
   */
  void addDroppedSample(uint32_t inflight_requests);

  uint32_t getSampleCount() const { return sample_count_; }
  uint32_t getMaxInFlightRequests() const { return max_inflight_requests_.get(); }
  std::chrono::nanoseconds getMinRtt() const { return min_rtt_.get(); }
  std::chrono::nanoseconds getAverageRtt() const {
    return sample_count_ == 0 ? std::chrono::nanoseconds(0)
                              : std::chrono::nanoseconds(sum_.count() / sample_count_);
  }
  bool didDrop() const { return did_drop_; }

private:
  MinimumMeasurement<std::chrono::nanoseconds> min_rtt_;
  std::chrono::nanoseconds sum_{};
  bool did_drop_{};
  uint32_t sample_count_{};
  MaximumMeasurement<uint32_t> max_inflight_requests_{};
};

} // namespace Common
} // namespace AdaptiveConcurrencyLimit
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy