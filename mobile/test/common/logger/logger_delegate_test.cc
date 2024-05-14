#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/api/external.h"
#include "library/common/bridge/utility.h"
#include "library/common/logger/logger_delegate.h"

using testing::_;
using testing::HasSubstr;
using testing::Not;

namespace Envoy {
namespace Logger {

class LambdaDelegateTest : public testing::Test {
public:
  static std::unique_ptr<EnvoyEventTracker> event_tracker;

  static void SetUpTestSuite() {
    Api::External::registerApi(std::string(ENVOY_EVENT_TRACKER_API_NAME), &event_tracker);
  }
};

std::unique_ptr<EnvoyEventTracker> LambdaDelegateTest::event_tracker =
    std::make_unique<EnvoyEventTracker>();

TEST_F(LambdaDelegateTest, LogCb) {
  std::string expected_msg = "Hello LambdaDelegate";
  std::string actual_msg;

  auto logger = std::make_unique<EnvoyLogger>();
  logger->on_log_ = [&](Logger::Levels, const std::string& message) { actual_msg = message; };
  LambdaDelegate delegate(std::move(logger), Registry::getSink());

  ENVOY_LOG_MISC(error, expected_msg);
  EXPECT_THAT(actual_msg, HasSubstr(expected_msg));
}

TEST_F(LambdaDelegateTest, LogCbWithLevels) {
  std::string unexpected_msg = "Hello NoLambdaDelegate";
  std::string expected_msg = "Hello LambdaDelegate";
  std::string actual_msg;

  auto logger = std::make_unique<EnvoyLogger>();
  logger->on_log_ = [&](Logger::Levels, const std::string& message) { actual_msg = message; };
  LambdaDelegate delegate(std::move(logger), Registry::getSink());

  // Set the log to critical. The message should not be logged.
  Context::changeAllLogLevels(spdlog::level::critical);
  ENVOY_LOG_MISC(error, unexpected_msg);
  EXPECT_THAT(actual_msg, Not(HasSubstr(unexpected_msg)));

  // Change to error. The message should be logged.
  Context::changeAllLogLevels(spdlog::level::err);
  ENVOY_LOG_MISC(error, expected_msg);
  EXPECT_THAT(actual_msg, HasSubstr(expected_msg));

  // Change back to critical and test one more time.
  Context::changeAllLogLevels(spdlog::level::critical);
  ENVOY_LOG_MISC(error, expected_msg);
  EXPECT_THAT(actual_msg, Not(HasSubstr(unexpected_msg)));
}

TEST_F(LambdaDelegateTest, ReleaseCb) {
  bool released = false;

  {
    auto logger = std::make_unique<EnvoyLogger>();
    logger->on_exit_ = [&] { released = true; };
    LambdaDelegate(std::move(logger), Registry::getSink());
  }

  EXPECT_TRUE(released);
}

class LambdaDelegateWithLevelTest
    : public testing::TestWithParam<std::tuple<Logger::Levels, spdlog::level::level_enum>> {};

INSTANTIATE_TEST_SUITE_P(
    LogLevel, LambdaDelegateWithLevelTest,
    testing::Values(std::make_tuple<>(Logger::Levels::trace, spdlog::level::trace),
                    std::make_tuple<>(Logger::Levels::debug, spdlog::level::debug),
                    std::make_tuple<>(Logger::Levels::info, spdlog::level::info),
                    std::make_tuple<>(Logger::Levels::warn, spdlog::level::warn),
                    std::make_tuple<>(Logger::Levels::error, spdlog::level::err),
                    std::make_tuple<>(Logger::Levels::critical, spdlog::level::critical)));

TEST_P(LambdaDelegateWithLevelTest, Log) {
  std::string expected_msg = "Hello LambdaDelegate";
  Logger::Levels actual_level;
  std::string actual_msg;
  auto logger = std::make_unique<EnvoyLogger>();
  logger->on_log_ = [&](Logger::Levels level, const std::string& message) {
    actual_level = level;
    actual_msg = message;
  };

  LambdaDelegate delegate(std::move(logger), Registry::getSink());

  Logger::Levels envoy_log_level = std::get<0>(GetParam());
  spdlog::level::level_enum spd_log_level = std::get<1>(GetParam());

  Context::changeAllLogLevels(spd_log_level);
  switch (envoy_log_level) {
  case Logger::Levels::trace:
    ENVOY_LOG_MISC(trace, expected_msg);
    break;
  case Logger::Levels::debug:
    ENVOY_LOG_MISC(debug, expected_msg);
    break;
  case Logger::Levels::info:
    ENVOY_LOG_MISC(info, expected_msg);
    break;
  case Logger::Levels::warn:
    ENVOY_LOG_MISC(warn, expected_msg);
    break;
  case Logger::Levels::error:
    ENVOY_LOG_MISC(error, expected_msg);
    break;
  case Logger::Levels::critical:
    ENVOY_LOG_MISC(critical, expected_msg);
    break;
  default:
    break;
  }
  EXPECT_THAT(actual_msg, HasSubstr(expected_msg));
  EXPECT_LE(actual_level, envoy_log_level);
}

} // namespace Logger
} // namespace Envoy
