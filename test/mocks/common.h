#pragma once

#include "envoy/common/time.h"

#include "test/precompiled/precompiled_test.h"

/**
 * This action allows us to save a reference parameter to a pointer target.
 */
ACTION_P(SaveArgAddress, target) { *target = &arg0; }

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

  MOCK_METHOD0(currentSystemTime, SystemTime());
};
