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
  virtual SystemTime currentTime() PURE;
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
  virtual MonotonicTime currentTime() PURE;
};

/**
 * Captures a system-time source, capable of computing both monotonically increasing
 * and real time.
 *
 * TODO(#4160): currently this is just a container for SystemTimeSource and
 * MonotonicTimeSource but we should clean that up and just have this as the
 * base class. Once that's done, TimeSource will be a pure interface.
 */
class TimeSource {
public:
  TimeSource(SystemTimeSource& system, MonotonicTimeSource& monotonic)
      : system_(system), monotonic_(monotonic) {}

  /**
   * @return the current system time; not guaranteed to be monotonically increasing.
   */
  SystemTime systemTime() { return system_.currentTime(); }

  /**
   * @return the current monotonic time.
   */
  MonotonicTime monotonicTime() { return monotonic_.currentTime(); }

  /**
   * Compares two time-sources for equality; this is needed for mocks.
   */
  bool operator==(const TimeSource& ts) const {
    return &system_ == &ts.system_ && &monotonic_ == &ts.monotonic_;
  }

  // TODO(jmarantz): Eliminate these methods and the SystemTimeSource and MonotonicTimeSource
  // classes, and change method calls to work directly off of TimeSource.
  SystemTimeSource& system() { return system_; }
  MonotonicTimeSource& monotonic() { return monotonic_; }

private:
  // These are pointers rather than references in order to support assignment.
  SystemTimeSource& system_;
  MonotonicTimeSource& monotonic_;
};

} // namespace Envoy
