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
  void deprecatedLogMessage() {
    log_trace("fake message");
    log_debug("fake message");
    conn_log_info("fake message", connection_);
    stream_log_info("fake message", stream_);
  }

  void logMessage() {
    LOG(trace, "fake message");
    LOG(debug, "fake message");
    CONN_LOG(info, "fake message", connection_);
    STREAM_LOG(info, "fake message", stream_);
  }

private:
  Envoy::Network::MockConnection connection_;
  Envoy::Http::MockStreamDecoderFilterCallbacks stream_;
};

TEST(Logger, All) {
  // This test exists just to ensure all macros compile and run with the expected arguments provided

  TestFilterLog filter;
  filter.logMessage();

  // Ensure the deprecated log macros are still operational.
  filter.deprecatedLogMessage();

  // Misc logging with no facility.
  LOG_MISC(info, "fake message");
}
} // Envoy