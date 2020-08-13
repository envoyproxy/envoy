#include <iostream>
#include <string>

#include "common/common/fancy_logger.h"
#include "common/common/logger.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/logging.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

class TestFilterLog : public Logger::Loggable<Logger::Id::filter> {
public:
  void logMessage() {
    ENVOY_LOG(trace, "fake message");
    ENVOY_LOG(debug, "fake message");
    ENVOY_LOG(warn, "fake message");
    ENVOY_LOG(error, "fake message");
    ENVOY_LOG(critical, "fake message");
    ENVOY_CONN_LOG(info, "fake message", connection_);
    ENVOY_STREAM_LOG(info, "fake message", stream_);
    ENVOY_CONN_LOG(error, "fake error", connection_);
    ENVOY_STREAM_LOG(error, "fake error", stream_);
  }

  void logMessageEscapeSequences() { ENVOY_LOG_MISC(info, "line 1 \n line 2 \t tab \\r test"); }

private:
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> stream_;
};

TEST(Logger, All) {
  // This test exists just to ensure all macros compile and run with the expected arguments provided

  TestFilterLog filter;
  filter.logMessage();

  // Misc logging with no facility.
  ENVOY_LOG_MISC(info, "fake message");
}

TEST(Logger, evaluateParams) {
  uint32_t i = 1;

  // Set logger's level to low level.
  // Log message with higher severity and make sure that params were evaluated.
  GET_MISC_LOGGER().set_level(spdlog::level::info);
  ENVOY_LOG_MISC(warn, "test message '{}'", i++);

  EXPECT_THAT(i, testing::Eq(2));
}

TEST(Logger, doNotEvaluateParams) {
  uint32_t i = 1;

  // Set logger's logging level high and log a message with lower severity
  // params should not be evaluated.
  GET_MISC_LOGGER().set_level(spdlog::level::critical);
  ENVOY_LOG_MISC(error, "test message '{}'", i++);
  EXPECT_THAT(i, testing::Eq(1));
}

TEST(Logger, logAsStatement) {
  // Just log as part of if ... statement
  uint32_t i = 1, j = 1;

  // Set logger's logging level to high
  GET_MISC_LOGGER().set_level(spdlog::level::critical);

  // Make sure that if statement inside of LOGGER macro does not catch trailing
  // else ....
  if (true) // NOLINT(readability-braces-around-statements)
    ENVOY_LOG_MISC(warn, "test message 1 '{}'", i++);
  else // NOLINT(readability-braces-around-statements)
    ENVOY_LOG_MISC(critical, "test message 2 '{}'", j++);

  EXPECT_THAT(i, testing::Eq(1));
  EXPECT_THAT(j, testing::Eq(1));

  // Do the same with curly brackets
  if (true) {
    ENVOY_LOG_MISC(warn, "test message 3 '{}'", i++);
  } else {
    ENVOY_LOG_MISC(critical, "test message 4 '{}'", j++);
  }

  EXPECT_THAT(i, testing::Eq(1));
  EXPECT_THAT(j, testing::Eq(1));
}

TEST(Logger, checkLoggerLevel) {
  class LogTestClass : public Logger::Loggable<Logger::Id::misc> {
  public:
    void setLevel(const spdlog::level::level_enum level) { ENVOY_LOGGER().set_level(level); }
    uint32_t executeAtTraceLevel() {
      if (ENVOY_LOG_CHECK_LEVEL(trace)) {
        //  Logger's level was at least trace
        return 1;
      } else {
        // Logger's level was higher than trace
        return 2;
      };
    }
  };

  LogTestClass test_obj;

  // Set Loggers severity low
  test_obj.setLevel(spdlog::level::trace);
  EXPECT_THAT(test_obj.executeAtTraceLevel(), testing::Eq(1));

  test_obj.setLevel(spdlog::level::info);
  EXPECT_THAT(test_obj.executeAtTraceLevel(), testing::Eq(2));
}

TEST(RegistryTest, LoggerWithName) {
  EXPECT_EQ(nullptr, Logger::Registry::logger("blah"));
  EXPECT_EQ("upstream", Logger::Registry::logger("upstream")->name());
}

class FormatTest : public testing::Test {
public:
  static void logMessageEscapeSequences() {
    ENVOY_LOG_MISC(info, "line 1 \n line 2 \t tab \\r test");
  }
};

TEST_F(FormatTest, OutputUnescaped) {
  const Envoy::ExpectedLogMessages message{{"info", "line 1 \n line 2 \t tab \\r test"}};
  EXPECT_LOG_CONTAINS_ALL_OF(message, logMessageEscapeSequences());
}

TEST_F(FormatTest, OutputEscaped) {
  // Note this uses a raw string literal
  const Envoy::ExpectedLogMessages message{{"info", R"(line 1 \n line 2 \t tab \\r test)"}};
  EXPECT_LOG_CONTAINS_ALL_OF_ESCAPED(message, logMessageEscapeSequences());
}

/**
 * Test for Fancy Logger convenient macros.
 */
TEST(Fancy, Global) {
  FANCY_LOG(info, "Hello world! Here's a line of fancy log!");
  FANCY_LOG(error, "Fancy Error! Here's the second message!");

  NiceMock<Network::MockConnection> connection_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> stream_;
  FANCY_CONN_LOG(warn, "Fake info {} of connection", connection_, 1);
  FANCY_STREAM_LOG(warn, "Fake warning {} of stream", stream_, 1);

  FANCY_LOG(critical, "Critical message for later flush.");
  FANCY_FLUSH_LOG();
}

TEST(Fancy, FastPath) {
  getFancyContext().setFancyLogger(__FILE__, spdlog::level::info);
  for (int i = 0; i < 10; i++) {
    FANCY_LOG(warn, "Fake warning No. {}", i);
  }
}

TEST(Fancy, SetLevel) {
  const char* file = "P=NP_file";
  bool res = getFancyContext().setFancyLogger(file, spdlog::level::trace);
  EXPECT_EQ(res, false);
  SpdLoggerSharedPtr p = getFancyContext().getFancyLogEntry(file);
  EXPECT_EQ(p, nullptr);

  res = getFancyContext().setFancyLogger(__FILE__, spdlog::level::err);
  EXPECT_EQ(res, true);
  FANCY_LOG(error, "Fancy Error! Here's a test for level.");
  FANCY_LOG(warn, "Warning: you shouldn't see this message!");
  p = getFancyContext().getFancyLogEntry(__FILE__);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::err);

  getFancyContext().setAllFancyLoggers(spdlog::level::info);
  FANCY_LOG(info, "Info: all loggers back to info.");
  FANCY_LOG(debug, "Debug: you shouldn't see this message!");
  EXPECT_EQ(getFancyContext().getFancyLogEntry(__FILE__)->level(), spdlog::level::info);
}

TEST(Fancy, Iteration) {
  FANCY_LOG(info, "Info: iteration test begins.");
  getFancyContext().setAllFancyLoggers(spdlog::level::info);
  std::string output = getFancyContext().listFancyLoggers();
  EXPECT_EQ(output, "   test/common/common/log_macros_test.cc: 2\n");
  std::string log_format = "[%T.%e][%t][%l][%n] %v";
  getFancyContext().setFancyLogger(__FILE__, spdlog::level::err);
  // setDefaultFancyLevelFormat relies on previous default and might cause error online
  // getFancyContext().setDefaultFancyLevelFormat(spdlog::level::warn, log_format);
  FANCY_LOG(warn, "Warning: now level is warning, format changed (Date removed).");
  FANCY_LOG(warn, getFancyContext().listFancyLoggers());
  // EXPECT_EQ(getFancyContext().getFancyLogEntry(__FILE__)->level(),
  //           spdlog::level::warn); // note fancy_default_level isn't changed
}

TEST(Fancy, Context) {
  FANCY_LOG(info, "Info: context API needs test.");
  bool enable_fine_grain_logging = Logger::Context::useFancyLogger();
  printf(" --> If use fancy logger: %d\n", enable_fine_grain_logging);
  if (enable_fine_grain_logging) {
    FANCY_LOG(critical, "Cmd option set: all previous Envoy Log should be converted now!");
  }
  Logger::Context::enableFancyLogger();
  EXPECT_EQ(Logger::Context::useFancyLogger(), true);
  EXPECT_EQ(Logger::Context::getFancyLogFormat(), "[%Y-%m-%d %T.%e][%t][%l] [%g:%#] %v");
  // EXPECT_EQ(Logger::Context::getFancyDefaultLevel(),
  //           spdlog::level::err); // default is error in test environment
}

} // namespace Envoy
