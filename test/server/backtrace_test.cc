#include <sstream>

#include "source/server/backtrace.h"

#include "test/test_common/logging.h"

#include "gmock/gmock.h"
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
  EXPECT_LOG_CONTAINS("critical", "Caught test signal, suspect faulting address",
                      tracer.logFault("test signal", &tracer));
  BackwardsTrace::setLogToStderr(save_log_to_stderr);
}

TEST(Backward, SingleEntry) {
  const bool save_log_to_stderr = BackwardsTrace::logToStderr();
  const bool save_single_line = BackwardsTrace::singleLine();
  BackwardsTrace::setLogToStderr(false);
  BackwardsTrace::setSingleLine(true);
  BackwardsTrace tracer;
  tracer.capture();
  {
    LogLevelSetter save_levels(spdlog::level::trace);
    StartStopRecording recording(GetLogSink());
    tracer.logTrace();
    auto messages = recording.messages();
    ASSERT_EQ(1, messages.size());
    EXPECT_THAT(messages[0], testing::HasSubstr("Backtrace"));
    EXPECT_THAT(messages[0], testing::HasSubstr("#0:"));
  }
  BackwardsTrace::setSingleLine(save_single_line);
  BackwardsTrace::setLogToStderr(save_log_to_stderr);
}

TEST(Backward, InvalidUsageTest) {
  // Ensure we do not crash if logging is attempted when there was no trace captured
  BackwardsTrace tracer;
  tracer.logTrace();
}

TEST(Backward, FaultToStderr) {
  const bool save_log_to_stderr = BackwardsTrace::logToStderr();
  BackwardsTrace::setLogToStderr(true);
  BackwardsTrace tracer;
  std::ostringstream expected;
  expected << "[backtrace] Caught Segmentation fault, suspect faulting address " << &tracer << "\n";
  {
    StartStopRecording recording(GetLogSink());
    testing::internal::CaptureStderr();
    tracer.logFault("Segmentation fault", &tracer);
    EXPECT_EQ(expected.str(), testing::internal::GetCapturedStderr());
    EXPECT_THAT(recording.messages(), testing::IsEmpty());
  }
  BackwardsTrace::setLogToStderr(save_log_to_stderr);
}

TEST(Backward, TraceToStderr) {
  const bool save_log_to_stderr = BackwardsTrace::logToStderr();
  const bool save_single_line = BackwardsTrace::singleLine();
  BackwardsTrace::setLogToStderr(true);
  BackwardsTrace tracer;
  tracer.capture();
  std::ostringstream expected;
  expected << "Backtrace (use tools/stack_decode.py to get line numbers):\n"
           << "Envoy version: " << VersionInfo::version() << "\n";
  if (!BackwardsTrace::addrMapping().empty()) {
    expected << "Address mapping: " << BackwardsTrace::addrMapping() << "\n";
  }
  for (bool single_line : {false, true}) {
    BackwardsTrace::setSingleLine(single_line);
    StartStopRecording recording(GetLogSink());
    testing::internal::CaptureStderr();
    tracer.logTrace();
    const std::string output = testing::internal::GetCapturedStderr();
    EXPECT_THAT(output, testing::StartsWith(expected.str()));
    EXPECT_THAT(output, testing::HasSubstr("#0 "));
    EXPECT_THAT(recording.messages(), testing::IsEmpty());
  }
  BackwardsTrace::setSingleLine(save_single_line);
  BackwardsTrace::setLogToStderr(save_log_to_stderr);
}
} // namespace Envoy
