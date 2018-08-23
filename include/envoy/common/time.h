#pragma once

#include <chrono>

#include "envoy/common/pure.h"

namespace Envoy {
/**
 * Less typing for common system time and steady time type.
 *
 * SystemTime should be used when getting a time to present to the user, e.g. for logging.
 * MonotonicTime should be used when tracking time for computing an interval.
 */
typedef std::chrono::time_point<std::chrono::system_clock> SystemTime;
typedef std::chrono::time_point<std::chrono::steady_clock> MonotonicTime;

/**
 * Abstraction for getting the current system time. Useful for testing.
 *
 * TODO(#4160): eliminate this class and pass TimeSource everywhere.
 */
class SystemTimeSource {
public:
  virtual ~SystemTimeSource() {}

  /**
   * @return the current system time.
   */
  virtual SystemTime currentTime() const PURE;
};

/**
 * Abstraction for getting the current monotonically increasing time. Useful for
 * testing.
 *
 * TODO(#4160): eliminate this class and pass TimeSource everywhere.
 */
class MonotonicTimeSource {
public:
  virtual ~MonotonicTimeSource() {}

  /**
   * @return the current monotonic time.
   */
  virtual MonotonicTime currentTime() const PURE;
};

/**
 * Captures a system-time source, capable of computing both monotonically increasing
 * and real time.
 *
 * TODO(#4160): currently this is just a container for SystemTimeSource and
 * MonotonicTimeSource but we should clean that up and just have this as the
 * base class.
 */
class TimeSource {
public:
  TimeSource(SystemTimeSource& system, MonotonicTimeSource& monotonic)
      : system_(&system), monotonic_(&monotonic) {}
  TimeSource(const TimeSource& src) : system_(src.system_), monotonic_(src.monotonic_) {}
  TimeSource& operator=(const TimeSource& src) {
    if (&src != this) {
      system_ = src.system_;
      monotonic_ = src.monotonic_;
    }
    return *this;
  }

  /**
   * @return the current system time; not guaranteed to be monotonically increasing.
   */
  SystemTime systemTime() const { return system_->currentTime(); }

  /**
   * @return the current monotonic time.
   */
  MonotonicTime monotonicTime() const { return monotonic_->currentTime(); }

  /**
   * Compares two time-sources for equality; this is needed for mocks.
   */
  bool operator==(const TimeSource& ts) const {
    return system_ == ts.system_ && monotonic_ == ts.monotonic_;
  }

  // TODO(jmarantz): Eliminate these methods and the SystemTimeSource and MonotonicTimeSource
  // classes, and change method calls to work directly off of TimeSource.
  SystemTimeSource& system() { return *system_; }
  MonotonicTimeSource& monotonic() { return *monotonic_; }

private:
  SystemTimeSource* system_;
  MonotonicTimeSource* monotonic_;
};

} // namespace Envoy
