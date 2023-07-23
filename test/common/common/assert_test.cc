#include "source/common/common/assert.h"

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
  // Use 2 assert action registrations to verify that action chaining is working correctly.
  int assert_fail_count = 0;
  int assert_fail_count2 = 0;
  auto debug_assert_action_registration =
      Assert::addDebugAssertionFailureRecordAction([&](const char*) { assert_fail_count++; });
  auto debug_assert_action_registration2 =
      Assert::addDebugAssertionFailureRecordAction([&](const char*) { assert_fail_count2++; });

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
#elif defined(ENVOY_LOG_FAST_DEBUG_ASSERT_IN_RELEASE)
  // ASSERTs always run when ENVOY_LOG_FAST_DEBUG_ASSERT_IN_RELEASE is compiled in.
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
  EXPECT_EQ(expected_counted_failures, assert_fail_count2);
}

TEST(AssertInReleaseTest, AssertLocation) {
#if defined(NDEBUG) && defined(ENVOY_LOG_FAST_DEBUG_ASSERT_IN_RELEASE)
  std::string assert_location;
  auto debug_assert_action_registration =
      Assert::addDebugAssertionFailureRecordAction([&](const char* location) {
        RELEASE_ASSERT(assert_location.empty(), "");
        assert_location = location;
      });
  ASSERT(false);

  auto expected_line = __LINE__ - 2;
  EXPECT_EQ(assert_location, absl::StrCat(__FILE__, ":", expected_line));
#endif
}

TEST(EnvoyBugStackTrace, TestStackTrace) {
  Assert::EnvoyBugStackTrace st;
  st.capture();
  EXPECT_LOG_CONTAINS("error", "stacktrace for envoy bug", st.logStackTrace());
  EXPECT_LOG_CONTAINS("error", "#0 ", st.logStackTrace());
}

TEST(EnvoyBugDeathTest, VariousLogs) {
  // Use 2 envoy bug action registrations to verify that action chaining is working correctly.
  int envoy_bug_fail_count = 0;
  int envoy_bug_fail_count2 = 0;
  // ENVOY_BUG actions only occur on power of two counts.
  auto envoy_bug_action_registration =
      Assert::addEnvoyBugFailureRecordAction([&](const char*) { envoy_bug_fail_count++; });
  auto envoy_bug_action_registration2 =
      Assert::addEnvoyBugFailureRecordAction([&](const char*) { envoy_bug_fail_count2++; });

  EXPECT_ENVOY_BUG({ ENVOY_BUG(false, ""); }, "envoy bug failure: false.");
  EXPECT_ENVOY_BUG({ ENVOY_BUG(false, ""); }, "envoy bug failure: false.");
  EXPECT_ENVOY_BUG({ ENVOY_BUG(false, "With some logs"); },
                   "envoy bug failure: false. Details: With some logs");
  EXPECT_ENVOY_BUG({ ENVOY_BUG(false, ""); }, "stacktrace for envoy bug");
  EXPECT_ENVOY_BUG({ ENVOY_BUG(false, ""); }, "#0 ");

#ifdef NDEBUG
  EXPECT_EQ(5, envoy_bug_fail_count);
  EXPECT_EQ(5, envoy_bug_fail_count2);
  // Reset envoy bug count to simplify testing exponential back-off below.
  envoy_bug_fail_count = 0;
  envoy_bug_fail_count2 = 0;
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
  EXPECT_EQ(5, envoy_bug_fail_count2);
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

TEST(EnvoyBugInReleaseTest, AssertLocation) {
#ifdef NDEBUG
  std::string envoy_bug_location;
  auto envoy_bug_action_registration =
      Assert::addEnvoyBugFailureRecordAction([&](const char* location) {
        RELEASE_ASSERT(envoy_bug_location.empty(), "");
        envoy_bug_location = location;
      });
  ENVOY_BUG(false, "message");

  auto expected_line = __LINE__ - 2;
  EXPECT_EQ(envoy_bug_location, absl::StrCat(__FILE__, ":", expected_line));
#endif
}

TEST(SlowAssertTest, TestSlowAssertInFastAssertInReleaseMode) {
  int expected_counted_failures;
  int slow_assert_fail_count = 0;
  auto debug_assert_action_registration =
      Assert::addDebugAssertionFailureRecordAction([&](const char*) { slow_assert_fail_count++; });

#ifndef NDEBUG
  EXPECT_DEATH({ SLOW_ASSERT(0); }, ".*assert failure: 0.*");
  EXPECT_DEATH({ SLOW_ASSERT(0, ""); }, ".*assert failure: 0.*");
  EXPECT_DEATH({ SLOW_ASSERT(0, "With some logs"); },
               ".*assert failure: 0. Details: With some logs.*");
  expected_counted_failures = 0;
#elif defined(ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE)
  // SLOW_ASSERTs are included in ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE
  EXPECT_LOG_CONTAINS("critical", "assert failure: 0", SLOW_ASSERT(0));
  EXPECT_LOG_CONTAINS("critical", "assert failure: 0", SLOW_ASSERT(0, ""));
  EXPECT_LOG_CONTAINS("critical", "assert failure: 0. Details: With some logs",
                      SLOW_ASSERT(0, "With some logs"));
  expected_counted_failures = 3;
#elif defined(ENVOY_LOG_FAST_DEBUG_ASSERT_IN_RELEASE) &&                                           \
    !defined(ENVOY_LOG_SLOW_DEBUG_ASSERT_IN_RELEASE)
  // Non-implementation for slow debug asserts when only ENVOY_LOG_FAST_DEBUG_ASSERT_IN_RELEASE is
  // compiled in.
  EXPECT_NO_LOGS(SLOW_ASSERT(0));
  EXPECT_NO_LOGS(SLOW_ASSERT(0, ""));
  EXPECT_NO_LOGS(SLOW_ASSERT(0, "With some logs"));
  expected_counted_failures = 0;
#else
  EXPECT_NO_LOGS(SLOW_ASSERT(0));
  EXPECT_NO_LOGS(SLOW_ASSERT(0, ""));
  EXPECT_NO_LOGS(SLOW_ASSERT(0, "With some logs"));
  expected_counted_failures = 0;
#endif

  EXPECT_EQ(expected_counted_failures, slow_assert_fail_count);
}

} // namespace Envoy
