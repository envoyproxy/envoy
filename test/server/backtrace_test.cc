#include "source/server/backtrace.h"

#include "test/test_common/logging.h"

#include "gtest/gtest.h"

namespace Envoy {
TEST(Backward, Basic) {
  // There isn't much to test here and this feature is really just useful for
  // debugging. This test simply verifies that we do not cause a crash when
  // logging a backtrace, and covers the added lines.
  const bool save_log_to_stderr = BackwardsTrace::logToStderr();
  BackwardsTrace::setLogToStderr(false);
  BackwardsTrace tracer;
  tracer.capture();
  EXPECT_LOG_CONTAINS("critical", "Envoy version:", tracer.logTrace());
  BackwardsTrace::setLogToStderr(save_log_to_stderr);
}

TEST(Backward, InvalidUsageTest) {
  // Ensure we do not crash if logging is attempted when there was no trace captured
  BackwardsTrace tracer;
  tracer.logTrace();
}
} // namespace Envoy
