#include "common/common/assert.h"

#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(ReleaseAssertDeathTest, VariousLogs) {
  EXPECT_DEATH({ RELEASE_ASSERT(0, ""); }, ".*assert failure: 0.*");
  EXPECT_DEATH({ RELEASE_ASSERT(0, "With some logs"); },
               ".*assert failure: 0. Details: With some logs.*");
  EXPECT_DEATH({ RELEASE_ASSERT(0 == EAGAIN, fmt::format("using {}", "fmt")); },
               ".*assert failure: 0 == EAGAIN. Details: using fmt.*");
}

TEST(AssertDeathTest, VariousLogs) {
  int expected_counted_failures;
  int assert_fail_count = 0;
  auto debug_assert_action_registration =
      Assert::setDebugAssertionFailureRecordAction([&]() { assert_fail_count++; });

#ifndef NDEBUG
  EXPECT_DEATH({ ASSERT(0); }, ".*assert failure: 0.*");
  EXPECT_DEATH({ ASSERT(0, ""); }, ".*assert failure: 0.*");
  EXPECT_DEATH({ ASSERT(0, "With some logs"); }, ".*assert failure: 0. Details: With some logs.*");
  expected_counted_failures = 0;
#elif defined(ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE)
  EXPECT_LOG_CONTAINS("critical", "assert failure: 0", ASSERT(0));
  EXPECT_LOG_CONTAINS("critical", "assert failure: 0", ASSERT(0, ""));
  EXPECT_LOG_CONTAINS("critical", "assert failure: 0. Details: With some logs",
                      ASSERT(0, "With some logs"));
  expected_counted_failures = 3;
#else
  EXPECT_NO_LOGS(ASSERT(0));
  EXPECT_NO_LOGS(ASSERT(0, ""));
  EXPECT_NO_LOGS(ASSERT(0, "With some logs"));
  expected_counted_failures = 0;
#endif

  EXPECT_EQ(expected_counted_failures, assert_fail_count);
}

TEST(EnvoyBugDeathTest, VariousLogs) {
  int envoy_bug_fail_count = 0;
  // ENVOY_BUG actions only occur on power of two counts.
  auto envoy_bug_action_registration =
      Assert::setEnvoyBugFailureRecordAction([&]() { envoy_bug_fail_count++; });

  EXPECT_ENVOY_BUG({ ENVOY_BUG(false, ""); }, "envoy bug failure: false.");
  EXPECT_ENVOY_BUG({ ENVOY_BUG(false, ""); }, "envoy bug failure: false.");
  EXPECT_ENVOY_BUG({ ENVOY_BUG(false, "With some logs"); },
                   "envoy bug failure: false. Details: With some logs");

#ifdef NDEBUG
  EXPECT_EQ(3, envoy_bug_fail_count);
  // Reset envoy bug count to simplify testing exponential back-off below.
  envoy_bug_fail_count = 0;
  // In release mode, same log lines trigger exponential back-off.
  for (int i = 0; i < 4; i++) {
    ENVOY_BUG(false, "placeholder ENVOY_BUG");
  }
  // 3 counts because 1st, 2nd, and 4th instances are powers of 2.
  EXPECT_EQ(3, envoy_bug_fail_count);

  // Different log lines have separate counters for exponential back-off.
  EXPECT_LOG_CONTAINS("error", "envoy bug failure: false", ENVOY_BUG(false, ""));
  EXPECT_LOG_CONTAINS("error", "envoy bug failure: false. Details: With some logs",
                      ENVOY_BUG(false, "With some logs"));
  EXPECT_EQ(5, envoy_bug_fail_count);
#endif
}

TEST(EnvoyBugDeathTest, TestResetCounters) {
  // The callEnvoyBug counter has already been called in assert2_test.cc.
  // ENVOY_BUG only log and increment stats on power of two cases. Ensure that counters are reset
  // between tests by checking that two consecutive calls trigger the expectation.
  for (int i = 0; i < 2; i++) {
    EXPECT_ENVOY_BUG(TestEnvoyBug::callEnvoyBug(), "envoy bug failure: false.");
  }
}

} // namespace Envoy
