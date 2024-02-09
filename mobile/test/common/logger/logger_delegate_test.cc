#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/api/external.h"
#include "library/common/data/utility.h"
#include "library/common/logger/logger_delegate.h"

using testing::_;
using testing::HasSubstr;
using testing::Not;

namespace Envoy {
namespace Logger {

class LambdaDelegateTest : public testing::Test {
public:
  static envoy_event_tracker tracker;

  static void SetUpTestSuite() {
    Api::External::registerApi(std::string(envoy_event_tracker_api_name), &tracker);
  }
};

envoy_event_tracker LambdaDelegateTest::tracker{};

TEST_F(LambdaDelegateTest, LogCb) {
  std::string expected_msg = "Hello LambdaDelegate";
  std::string actual_msg;

  LambdaDelegate delegate({[](envoy_log_level, envoy_data data, const void* context) -> void {
                             auto* actual_msg =
                                 static_cast<std::string*>(const_cast<void*>(context));
                             *actual_msg = Data::Utility::copyToString(data);
                             release_envoy_data(data);
                           },
                           [](const void*) -> void {}, &actual_msg},
                          Registry::getSink());

  ENVOY_LOG_MISC(error, expected_msg);
  EXPECT_THAT(actual_msg, HasSubstr(expected_msg));
}

TEST_F(LambdaDelegateTest, LogCbWithLevels) {
  std::string unexpected_msg = "Hello NoLambdaDelegate";
  std::string expected_msg = "Hello LambdaDelegate";
  std::string actual_msg;

  LambdaDelegate delegate({[](envoy_log_level, envoy_data data, const void* context) -> void {
                             auto* actual_msg =
                                 static_cast<std::string*>(const_cast<void*>(context));
                             *actual_msg = Data::Utility::copyToString(data);
                             release_envoy_data(data);
                           },
                           [](const void*) -> void {}, &actual_msg},
                          Registry::getSink());

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
    LambdaDelegate(
        {[](envoy_log_level, envoy_data data, const void*) -> void { release_envoy_data(data); },
         [](const void* context) -> void {
           bool* released = static_cast<bool*>(const_cast<void*>(context));
           *released = true;
         },
         &released},
        Registry::getSink());
  }

  EXPECT_TRUE(released);
}

class LambdaDelegateWithLevelTest
    : public testing::TestWithParam<
          std::tuple<envoy_log_level, Logger::Levels, spdlog::level::level_enum>> {};

INSTANTIATE_TEST_SUITE_P(
    LogLevel, LambdaDelegateWithLevelTest,
    testing::Values(std::make_tuple<>(envoy_log_level::ENVOY_LOG_LEVEL_TRACE, Logger::Levels::trace,
                                      spdlog::level::trace),
                    std::make_tuple<>(envoy_log_level::ENVOY_LOG_LEVEL_DEBUG, Logger::Levels::debug,
                                      spdlog::level::debug),
                    std::make_tuple<>(envoy_log_level::ENVOY_LOG_LEVEL_INFO, Logger::Levels::info,
                                      spdlog::level::info),
                    std::make_tuple<>(envoy_log_level::ENVOY_LOG_LEVEL_WARN, Logger::Levels::warn,
                                      spdlog::level::warn),
                    std::make_tuple<>(envoy_log_level::ENVOY_LOG_LEVEL_ERROR, Logger::Levels::error,
                                      spdlog::level::err),
                    std::make_tuple<>(envoy_log_level::ENVOY_LOG_LEVEL_CRITICAL,
                                      Logger::Levels::critical, spdlog::level::critical)));

TEST_P(LambdaDelegateWithLevelTest, Log) {
  std::string expected_msg = "Hello LambdaDelegate";
  struct Actual {
    envoy_log_level level;
    std::string msg;
  };
  Actual actual;

  LambdaDelegate delegate({[](envoy_log_level level, envoy_data data, const void* context) -> void {
                             auto* actual = static_cast<Actual*>(const_cast<void*>(context));
                             actual->msg = Data::Utility::copyToString(data);
                             actual->level = level;
                             release_envoy_data(data);
                           },
                           [](const void*) -> void {}, &actual},
                          Registry::getSink());

  envoy_log_level c_envoy_log_level = std::get<0>(GetParam());
  Logger::Levels envoy_log_level = std::get<1>(GetParam());
  spdlog::level::level_enum spd_log_level = std::get<2>(GetParam());

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
  EXPECT_THAT(actual.msg, HasSubstr(expected_msg));
  EXPECT_LE(actual.level, c_envoy_log_level);
}

} // namespace Logger
} // namespace Envoy
