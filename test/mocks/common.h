#pragma once

#include "envoy/common/time.h"

#include "gmock/gmock.h"

namespace Envoy {
/**
 * This action allows us to save a reference parameter to a pointer target.
 */
ACTION_P(SaveArgAddress, target) { *target = &arg0; }

/**
 * Matcher that matches on whether the pointee of both lhs and rhs are equal.
 */
MATCHER_P(PointeesEq, rhs, "") {
  *result_listener << testing::PrintToString(*arg) + " != " + testing::PrintToString(*rhs);
  return *arg == *rhs;
}

/**
 * Simple mock that just lets us make sure a method gets called or not called form a lambda.
 */
class ReadyWatcher {
public:
  ReadyWatcher();
  ~ReadyWatcher();

  MOCK_METHOD0(ready, void());
};

class MockSystemTimeSource : public SystemTimeSource {
public:
  MockSystemTimeSource();
  ~MockSystemTimeSource();

  MOCK_METHOD0(currentTime, SystemTime());
};

class MockMonotonicTimeSource : public MonotonicTimeSource {
public:
  MockMonotonicTimeSource();
  ~MockMonotonicTimeSource();

  MOCK_METHOD0(currentTime, MonotonicTime());
};
} // namespace Envoy
