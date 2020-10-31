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

TEST(LoggerCustomFlagsTest, SetFormatter) {
  using PatternFormatterPtr = std::unique_ptr<spdlog::pattern_formatter>;

  auto create_formatter = [](const std::string& pattern) -> PatternFormatterPtr {
    auto formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter
        ->add_flag<CustomFlagFormatter::EscapeMessageNewLine>(
            CustomFlagFormatter::EscapeMessageNewLine::Placeholder)
        .set_pattern(pattern);
    return formatter;
  };

  spdlog::details::log_msg msg("test", spdlog::level::info, "\n\nmessage\n\n");

  {
    auto logger = DelegatingLogSink::init();
    // This uses "%v", the default flag for printing the actual text to log.
    // https://github.com/gabime/spdlog/wiki/3.-Custom-formatting#pattern-flags.
    auto formatter = create_formatter("%v");
    logger->set_formatter(std::move(formatter));

    testing::internal::CaptureStderr();
    logger->log(msg);
#ifdef WIN32
    EXPECT_EQ("\n\nmessage\n\n\r\n", testing::internal::GetCapturedStderr());
#else
    EXPECT_EQ("\n\nmessage\n\n\n", testing::internal::GetCapturedStderr());
#endif
    logger.reset();
  }

  {
    auto escape_newline_logger = DelegatingLogSink::init();
    // This uses "%_", the added custom flag that escapes newlines from the actual text to log.
    auto escape_newline_formatter = create_formatter("%_");
    escape_newline_logger->set_formatter(std::move(escape_newline_formatter));

    testing::internal::CaptureStderr();
    escape_newline_logger->log(msg);
#ifdef WIN32
    EXPECT_EQ("\\n\\nmessage\\n\\n\r\n", testing::internal::GetCapturedStderr());
#else
    EXPECT_EQ("\\n\\nmessage\\n\\n\n", testing::internal::GetCapturedStderr());
#endif
    escape_newline_logger.reset();
  }
}

} // namespace Logger
} // namespace Envoy
