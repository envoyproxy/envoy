#include <memory>
#include <string>

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

} // namespace Logger
} // namespace Envoy
