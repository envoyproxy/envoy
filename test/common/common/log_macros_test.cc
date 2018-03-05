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
} // namespace Envoy
