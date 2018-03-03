#include <iostream>
#include <string>

#include "common/common/logger.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"

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
  }

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

  // set logger's level to low level.
  // log message with higher severity and make sure that params were evaluated.
  GET_MISC_LOGGER().set_level(spdlog::level::info);
  ENVOY_LOG_MISC(warn, "test message '{}'", i++);
  ASSERT_THAT(i, testing::Eq(2));
}

TEST(Logger, doNotEvaluateParams) {
  uint32_t i = 1;

  // set logger's logging level high and log a message with lower severity
  // params should not be evaluated.
  GET_MISC_LOGGER().set_level(spdlog::level::critical);
  ENVOY_LOG_MISC(error, "test message '{}'", i++);
  ASSERT_THAT(i, testing::Eq(1));
}

TEST(Logger, logAsStatement) {
  // Just log as part of if ... statement

  if (true)
    ENVOY_LOG_MISC(critical, "test message 1");
  else
    ENVOY_LOG_MISC(critical, "test message 2");

  // do the same with curly brackets
  if (true) {
    ENVOY_LOG_MISC(critical, "test message 3");
  } else {
    ENVOY_LOG_MISC(critical, "test message 4");
  }
}
} // namespace Envoy
