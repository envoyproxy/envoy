#include <string>

#include "common/common/logger.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

class LoggerEscapeTest : public testing::Test {};

TEST_F(LoggerEscapeTest, LinuxEOL) {
  const std::string log = Logger::DelegatingLogSink::escapeLogLine("line 1 \n line 2\n");
  EXPECT_EQ("line 1 \\n line 2\n", log);
}

TEST_F(LoggerEscapeTest, WindowEOL) {
  const std::string log = Logger::DelegatingLogSink::escapeLogLine("line 1 \n line 2\r\n");
  EXPECT_EQ("line 1 \\n line 2\r\n", log);
}

TEST_F(LoggerEscapeTest, NoTrailingWhitespace) {
  const std::string log = Logger::DelegatingLogSink::escapeLogLine("line 1 \n line 2");
  EXPECT_EQ("line 1 \\n line 2", log);
}

TEST_F(LoggerEscapeTest, NoWhitespace) {
  const std::string log = Logger::DelegatingLogSink::escapeLogLine("line1");
  EXPECT_EQ("line1", log);
}

TEST_F(LoggerEscapeTest, AnyTrailingWhitespace) {
  const std::string log = Logger::DelegatingLogSink::escapeLogLine("line 1 \t tab 1 \n line 2\t\n");
  EXPECT_EQ("line 1 \\t tab 1 \\n line 2\t\n", log);
}

TEST_F(LoggerEscapeTest, WhitespaceOnly) {
  // 8 spaces
  const std::string log1 = Logger::DelegatingLogSink::escapeLogLine("        ");
  EXPECT_EQ("        ", log1);

  // Any whitespace characters
  const std::string log2 = Logger::DelegatingLogSink::escapeLogLine("\r\n\t \r\n \n");
  EXPECT_EQ("\r\n\t \r\n \n", log2);
}

} // namespace Envoy
