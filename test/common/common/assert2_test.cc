#include "source/common/common/assert.h"

#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(EnvoyBugDeathTest, TestResetCounters) {
  // The callEnvoyBug counter has already been called in assert2_test.cc.
  // ENVOY_BUG only log and increment stats on power of two cases. Ensure that counters are reset
  // between tests by checking that two consecutive calls trigger the expectation.
  for (int i = 0; i < 2; i++) {
    EXPECT_ENVOY_BUG(TestEnvoyBug::callEnvoyBug(), "envoy bug failure: false.");
  }
}

} // namespace Envoy
