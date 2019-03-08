#include "common/common/assert.h"

#include "test/test_common/logging.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(ReleaseAssertDeathTest, VariousLogs) {
  Logger::StderrSinkDelegate stderr_sink(Logger::Registry::getSink()); // For coverage build.
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

} // namespace Envoy
