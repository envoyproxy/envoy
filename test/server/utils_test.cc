#include "source/server/utils.h"

#include "test/mocks/server/options.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Server {
namespace Utility {

// Most utils paths are covered through other tests, these tests take of
// of special cases to get remaining coverage.
TEST(UtilsTest, BadServerState) {
  Utility::serverState(Init::Manager::State::Uninitialized, true);
  EXPECT_ENVOY_BUG(Utility::serverState(static_cast<Init::Manager::State>(123), true),
                   "unexpected server state");
}

TEST(UtilsTest, AssertExclusiveLogFormatMethod) {
  {
    testing::NiceMock<MockOptions> options;
    envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig log_config;
    EXPECT_TRUE(Utility::assertExclusiveLogFormatMethod(options, log_config).ok());
  }

  {
    testing::NiceMock<MockOptions> options;
    envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig log_config;
    EXPECT_CALL(options, logFormatSet()).WillRepeatedly(Return(true));
    EXPECT_TRUE(Utility::assertExclusiveLogFormatMethod(options, log_config).ok());
  }

  {
    testing::NiceMock<MockOptions> options;
    envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig log_config;
    log_config.mutable_log_format();
    EXPECT_TRUE(Utility::assertExclusiveLogFormatMethod(options, log_config).ok());
  }

  {
    testing::NiceMock<MockOptions> options;
    envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig log_config;
    EXPECT_CALL(options, logFormatSet()).WillRepeatedly(Return(true));
    log_config.mutable_log_format();
    EXPECT_EQ(
        Utility::assertExclusiveLogFormatMethod(options, log_config).message(),
        "Only one of ApplicationLogConfig.log_format or CLI option --log-format can be specified.");
  }
}

TEST(UtilsTest, MaybeSetApplicationLogFormat) {
  {
    envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig log_config;
    EXPECT_TRUE(Utility::maybeSetApplicationLogFormat(log_config).ok());
  }

  {
    envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig log_config;
    log_config.mutable_log_format();
    EXPECT_TRUE(Utility::maybeSetApplicationLogFormat(log_config).ok());
  }

  {
    envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig log_config;
    log_config.mutable_log_format()->mutable_json_format();
    EXPECT_TRUE(Utility::maybeSetApplicationLogFormat(log_config).ok());
  }

  {
    envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig log_config;
    log_config.mutable_log_format()->mutable_text_format();
    EXPECT_TRUE(Utility::maybeSetApplicationLogFormat(log_config).ok());
  }

  {
    envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig log_config;
    auto* format = log_config.mutable_log_format()->mutable_json_format();
    format->mutable_fields()->operator[]("Message").set_string_value("%v");
    EXPECT_EQ(Utility::maybeSetApplicationLogFormat(log_config).message(),
              "setJsonLogFormat error: INVALID_ARGUMENT: Usage of %v is "
              "unavailable for JSON log formats");
  }

  {
    envoy::config::bootstrap::v3::Bootstrap::ApplicationLogConfig log_config;
    auto* format = log_config.mutable_log_format()->mutable_json_format();
    format->mutable_fields()->operator[]("Message").set_string_value("%_");
    EXPECT_EQ(Utility::maybeSetApplicationLogFormat(log_config).message(),
              "setJsonLogFormat error: INVALID_ARGUMENT: Usage of %_ is "
              "unavailable for JSON log formats");
  }
}

} // namespace Utility
} // namespace Server
} // namespace Envoy
