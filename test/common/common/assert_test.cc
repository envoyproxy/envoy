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
  EXPECT_DEATH({ ASSERT_OR_LOG(0); }, ".assert failure: 0*");
  expected_counted_failures = 0;
#elif defined(ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE)
  EXPECT_LOG_CONTAINS("critical", "assert failure: 0", ASSERT(0));
  EXPECT_LOG_CONTAINS("critical", "assert failure: 0", ASSERT(0, ""));
  EXPECT_LOG_CONTAINS("critical", "assert failure: 0. Details: With some logs",
                      ASSERT(0, "With some logs"));
  EXPECT_LOG_CONTAINS("critical", "assert failure: 0. Details: ASSERT_OR_LOG()", ASSERT_OR_LOG(0));
  expected_counted_failures = 4;
#else
  EXPECT_NO_LOGS(ASSERT(0));
  EXPECT_NO_LOGS(ASSERT(0, ""));
  EXPECT_NO_LOGS(ASSERT(0, "With some logs"));
  EXPECT_LOG_CONTAINS("error", "ASSERT_OR_LOG(0)", ASSERT_OR_LOG(0));
  expected_counted_failures = 0;
#endif

  EXPECT_EQ(expected_counted_failures, assert_fail_count);
}

TEST(AssertDeathTest, LogDfatalOrReturn) {
#ifndef NDEBUG
  EXPECT_DEATH({ ASSERT_OR_LOG_AND(0, return ); }, ".assert failure: 0*");
#elif defined(ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE)
  EXPECT_LOG_CONTAINS("critical", "assert failure: 0. Details: ASSERT_OR_LOG_AND(return)",
                      ASSERT_OR_LOG_AND(0, return ));
#else
  EXPECT_LOG_CONTAINS("error", "ASSERT_OR_LOG_AND(0)", ASSERT_OR_LOG_AND(0, return ));
  EXPECT_TRUE(false) << "statement should not be reached due to _OR";
#endif
}

TEST(AssertDeathTest, LogDfatalOrBreak) {
  uint32_t count = 0;
  const uint32_t iters = 10;
  for (uint32_t i = 0; i < iters; ++i) {
    ++count;
#ifndef NDEBUG
    EXPECT_DEATH({ ASSERT_OR_LOG_AND(0, break); }, ".assert failure: 0*");
#elif defined(ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE)
    EXPECT_LOG_CONTAINS("critical", "assert failure: 0. Details: ASSERT_OR_LOG_AND(break)",
                        ASSERT_OR_LOG_AND(0, break));
#else
    EXPECT_LOG_CONTAINS("error", "ASSERT_OR_LOG_AND(0)", ASSERT_OR_LOG_AND(0, return ));
    EXPECT_TRUE(false) << "statement should not be reached due to _OR";
#endif
  }

#ifndef NDEBUG
  // For debug death-tests, the break will not occur, there will be an assertion
  // which is trapped by the EXPECT_DEATH macro.
  EXPECT_EQ(iters, count);
#elif defined(ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE)
  EXPECT_EQ(iters, count);
#else
  // For debug builds, the break occurs, so the loop only runs once.
  EXPECT_EQ(1, count);
#endif
}

} // namespace Envoy
