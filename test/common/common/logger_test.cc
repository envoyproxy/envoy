#include <string>

#include "common/common/logger.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Logger {

class LoggerEscapeTest : public testing::Test {};

TEST_F(LoggerEscapeTest, LinuxEOL) {
  EXPECT_EQ("line 1 \\n line 2\n", DelegatingLogSink::escapeLogLine("line 1 \n line 2\n"));
}

TEST_F(LoggerEscapeTest, WindowEOL) {
  EXPECT_EQ("line 1 \\n line 2\r\n", DelegatingLogSink::escapeLogLine("line 1 \n line 2\r\n"));
}

TEST_F(LoggerEscapeTest, NoTrailingWhitespace) {
  EXPECT_EQ("line 1 \\n line 2", DelegatingLogSink::escapeLogLine("line 1 \n line 2"));
}

TEST_F(LoggerEscapeTest, NoWhitespace) {
  EXPECT_EQ("line1", DelegatingLogSink::escapeLogLine("line1"));
}

TEST_F(LoggerEscapeTest, AnyTrailingWhitespace) {
  EXPECT_EQ("line 1 \\t tab 1 \\n line 2\t\n",
            DelegatingLogSink::escapeLogLine("line 1 \t tab 1 \n line 2\t\n"));
}

TEST_F(LoggerEscapeTest, WhitespaceOnly) {
  // 8 spaces
  EXPECT_EQ("        ", DelegatingLogSink::escapeLogLine("        "));

  // Any whitespace characters
  EXPECT_EQ("\r\n\t \r\n \n", DelegatingLogSink::escapeLogLine("\r\n\t \r\n \n"));
}

TEST_F(LoggerEscapeTest, Empty) { EXPECT_EQ("", DelegatingLogSink::escapeLogLine("")); }

} // namespace Logger
} // namespace Envoy
