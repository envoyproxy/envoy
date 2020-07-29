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

} // namespace Logger
} // namespace Envoy
