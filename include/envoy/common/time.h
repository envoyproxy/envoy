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
 * Captures a system-time source, capable of computing both monotonically increasing
 * and real time.
 */
class TimeSource {
public:
  virtual ~TimeSource() {}

  /**
   * @return the current system time; not guaranteed to be monotonically increasing.
   */
  virtual SystemTime systemTime() PURE;
  /**
   * @return the current monotonic time.
   */
  virtual MonotonicTime monotonicTime() PURE;
};

} // namespace Envoy
