#include <iostream>
#include <sstream>

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

TEST(Backward, FindsBaseAddress) {
  BackwardsTrace tracer;
  // Envoy base address always gets logged -- so we don't need to bother
  // setting up a real trace.
  std::stringstream logged;
  tracer.printTrace(logged);
  auto as_string = logged.str();
  std::vector<absl::string_view> lines = absl::StrSplit(as_string, '\n');
  // Assert the first line is a base offset, and contains a hex start.
  EXPECT_FALSE(lines.empty());
  EXPECT_NE(lines[0].find("ENVOY_BASE_OFFSET: 0x"), absl::string_view::npos);
}

TEST(Backward, InvalidUsageTest) {
  // Ensure we do not crash if logging is attempted when there was no trace captured
  BackwardsTrace tracer;
  tracer.logTrace();
}
} // namespace Envoy
