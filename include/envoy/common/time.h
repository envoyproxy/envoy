#pragma once

#include "envoy/common/pure.h"

/**
 * Less typing for common system time type.
 */
typedef std::chrono::time_point<std::chrono::system_clock> SystemTime;

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
