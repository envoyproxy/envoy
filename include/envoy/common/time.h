#pragma once

#include "envoy/common/pure.h"

/**
 * Less typing for common system time type.
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
  virtual SystemTime currentSystemTime() PURE;
};

class MonotonicTimeSource {
public:
  virtual ~MonotonicTimeSource() {}

  /**
   * @return the current monotonic time.
   */
  virtual MonotonicTime currentSystemTime() PURE;
};