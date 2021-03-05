#include <memory>
#include <string>

#include "common/common/json_escape_string.h"
#include "common/common/logger.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Logger {

TEST(LoggerTest, StackingStderrSinkDelegate) {
  StderrSinkDelegate stacked(Envoy::Logger::Registry::getSink());
}

TEST(LoggerEscapeTest, LinuxEOL) {
  EXPECT_EQ("line 1 \\n line 2\n", DelegatingLogSink::escapeLogLine("line 1 \n line 2\n"));
}

TEST(LoggerEscapeTest, WindowEOL) {
  EXPECT_EQ("line 1 \\n line 2\r\n", DelegatingLogSink::escapeLogLine("line 1 \n line 2\r\n"));
}

TEST(LoggerEscapeTest, NoTrailingWhitespace) {
  EXPECT_EQ("line 1 \\n line 2", DelegatingLogSink::escapeLogLine("line 1 \n line 2"));
}

TEST(LoggerEscapeTest, NoWhitespace) {
  EXPECT_EQ("line1", DelegatingLogSink::escapeLogLine("line1"));
}

TEST(LoggerEscapeTest, AnyTrailingWhitespace) {
  EXPECT_EQ("line 1 \\t tab 1 \\n line 2\t\n",
            DelegatingLogSink::escapeLogLine("line 1 \t tab 1 \n line 2\t\n"));
}

TEST(LoggerEscapeTest, WhitespaceOnly) {
  // 8 spaces
  EXPECT_EQ("        ", DelegatingLogSink::escapeLogLine("        "));

  // Any whitespace characters
  EXPECT_EQ("\r\n\t \r\n \n", DelegatingLogSink::escapeLogLine("\r\n\t \r\n \n"));
}

TEST(LoggerEscapeTest, Empty) { EXPECT_EQ("", DelegatingLogSink::escapeLogLine("")); }

TEST(JsonEscapeTest, Escape) {
  const auto expect_json_escape = [](absl::string_view to_be_escaped, absl::string_view escaped) {
    EXPECT_EQ(escaped,
              JsonEscaper::escapeString(to_be_escaped, JsonEscaper::extraSpace(to_be_escaped)));
  };

  expect_json_escape("\"", "\\\"");
  expect_json_escape("\\", "\\\\");
  expect_json_escape("\b", "\\b");
  expect_json_escape("\f", "\\f");
  expect_json_escape("\n", "\\n");
  expect_json_escape("\r", "\\r");
  expect_json_escape("\t", "\\t");
  expect_json_escape("\x01", "\\u0001");
  expect_json_escape("\x02", "\\u0002");
  expect_json_escape("\x03", "\\u0003");
  expect_json_escape("\x04", "\\u0004");
  expect_json_escape("\x05", "\\u0005");
  expect_json_escape("\x06", "\\u0006");
  expect_json_escape("\x07", "\\u0007");
  expect_json_escape("\x08", "\\b");
  expect_json_escape("\x09", "\\t");
  expect_json_escape("\x0a", "\\n");
  expect_json_escape("\x0b", "\\u000b");
  expect_json_escape("\x0c", "\\f");
  expect_json_escape("\x0d", "\\r");
  expect_json_escape("\x0e", "\\u000e");
  expect_json_escape("\x0f", "\\u000f");
  expect_json_escape("\x10", "\\u0010");
  expect_json_escape("\x11", "\\u0011");
  expect_json_escape("\x12", "\\u0012");
  expect_json_escape("\x13", "\\u0013");
  expect_json_escape("\x14", "\\u0014");
  expect_json_escape("\x15", "\\u0015");
  expect_json_escape("\x16", "\\u0016");
  expect_json_escape("\x17", "\\u0017");
  expect_json_escape("\x18", "\\u0018");
  expect_json_escape("\x19", "\\u0019");
  expect_json_escape("\x1a", "\\u001a");
  expect_json_escape("\x1b", "\\u001b");
  expect_json_escape("\x1c", "\\u001c");
  expect_json_escape("\x1d", "\\u001d");
  expect_json_escape("\x1e", "\\u001e");
  expect_json_escape("\x1f", "\\u001f");
}

class LoggerCustomFlagsTest : public testing::Test {
public:
  LoggerCustomFlagsTest() : logger_(Registry::getSink()) {}

  void expectLogMessage(const std::string& pattern, const std::string& message,
                        const std::string& expected) {
    auto formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter
        ->add_flag<CustomFlagFormatter::EscapeMessageNewLine>(
            CustomFlagFormatter::EscapeMessageNewLine::Placeholder)
        .set_pattern(pattern);
    formatter
        ->add_flag<CustomFlagFormatter::EscapeMessageJsonString>(
            CustomFlagFormatter::EscapeMessageJsonString::Placeholder)
        .set_pattern(pattern);
    logger_->set_formatter(std::move(formatter));

    testing::internal::CaptureStderr();
    logger_->log(spdlog::details::log_msg("test", spdlog::level::info, message));
#ifdef WIN32
    EXPECT_EQ(expected + "\r\n", testing::internal::GetCapturedStderr());
#else
    EXPECT_EQ(expected + "\n", testing::internal::GetCapturedStderr());
#endif
  }

protected:
  DelegatingLogSinkSharedPtr logger_;
};

TEST_F(LoggerCustomFlagsTest, LogMessageAsIs) {
  // This uses "%v", the default flag for printing the actual text to log.
  // https://github.com/gabime/spdlog/wiki/3.-Custom-formatting#pattern-flags.
  expectLogMessage("%v", "\n\nmessage\n\n", "\n\nmessage\n\n");
}

TEST_F(LoggerCustomFlagsTest, LogMessageAsEscaped) {
  // This uses "%_", the added custom flag that escapes newlines from the actual text to log.
  expectLogMessage("%_", "\n\nmessage\n\n", "\\n\\nmessage\\n\\n");
}

TEST_F(LoggerCustomFlagsTest, LogMessageAsJsonStringEscaped) {
  // This uses "%j", the added custom flag that JSON escape the characters inside the log message
  // payload.
  expectLogMessage("%j", "message", "message");
  expectLogMessage("%j", "\n\nmessage\n\n", "\\n\\nmessage\\n\\n");
  expectLogMessage("%j", "\bok\b", "\\bok\\b");
  expectLogMessage("%j", "\fok\f", "\\fok\\f");
  expectLogMessage("%j", "\rok\r", "\\rok\\r");
  expectLogMessage("%j", "\tok\t", "\\tok\\t");
  expectLogMessage("%j", "\\ok\\", "\\\\ok\\\\");
  expectLogMessage("%j", "\"ok\"", "\\\"ok\\\"");
  expectLogMessage("%j", "\x01ok\x0e", "\\u0001ok\\u000e");
  expectLogMessage(
      "%j",
      "StreamAggregatedResources gRPC config stream closed: 14, connection error: desc = "
      "\"transport: Error while dialing dial tcp [::1]:15012: connect: connection refused\"",
      "StreamAggregatedResources gRPC config stream closed: 14, connection error: desc = "
      "\\\"transport: Error while dialing dial tcp [::1]:15012: connect: connection refused\\\"");
}

} // namespace Logger
} // namespace Envoy
