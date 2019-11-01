#include <string>

#include "common/common/logger.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

class LoggerEscapeTest : public testing::Test {};

TEST_F(LoggerEscapeTest, LinuxEOL) {
  EXPECT_EQ("line 1 \\n line 2\n", Logger::DelegatingLogSink::escapeLogLine("line 1 \n line 2\n"));
}

TEST_F(LoggerEscapeTest, WindowEOL) {
  EXPECT_EQ("line 1 \\n line 2\r\n",
            Logger::DelegatingLogSink::escapeLogLine("line 1 \n line 2\r\n"));
}

TEST_F(LoggerEscapeTest, NoTrailingWhitespace) {
  EXPECT_EQ("line 1 \\n line 2", Logger::DelegatingLogSink::escapeLogLine("line 1 \n line 2"));
}

TEST_F(LoggerEscapeTest, NoWhitespace) {
  EXPECT_EQ("line1", Logger::DelegatingLogSink::escapeLogLine("line1"));
}

TEST_F(LoggerEscapeTest, AnyTrailingWhitespace) {
  EXPECT_EQ("line 1 \\t tab 1 \\n line 2\t\n",
            Logger::DelegatingLogSink::escapeLogLine("line 1 \t tab 1 \n line 2\t\n"));
}

TEST_F(LoggerEscapeTest, WhitespaceOnly) {
  // 8 spaces
  EXPECT_EQ("        ", Logger::DelegatingLogSink::escapeLogLine("        "));

  // Any whitespace characters
  EXPECT_EQ("\r\n\t \r\n \n", Logger::DelegatingLogSink::escapeLogLine("\r\n\t \r\n \n"));
}

TEST_F(LoggerEscapeTest, Empty) { EXPECT_EQ("", Logger::DelegatingLogSink::escapeLogLine("")); }

} // namespace Envoy
