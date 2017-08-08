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
 * Abstraction for getting the current monotonically increasing time. Useful for testing.
 */
class MonotonicTimeSource {
public:
  virtual ~MonotonicTimeSource() {}

  /**
   * @return the current monotonic time.
   */
  virtual MonotonicTime currentTime() PURE;
};
} // namespace Envoy
