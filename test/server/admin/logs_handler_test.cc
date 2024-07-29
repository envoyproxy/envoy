#include "source/common/common/fine_grain_logger.h"
#include "source/common/common/logger.h"

#include "test/server/admin/admin_instance.h"

using testing::HasSubstr;
using testing::IsNull;

namespace Envoy {
namespace Server {

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminInstanceTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminInstanceTest, ReopenLogs) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;

  EXPECT_CALL(server_, accessLogManager()).WillRepeatedly(ReturnRef(access_log_manager_));
  EXPECT_CALL(access_log_manager_, reopen());
  EXPECT_EQ(Http::Code::OK, postCallback("/reopen_logs", header_map, response));
}

TEST_P(AdminInstanceTest, LogLevelSetting) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  // now for Envoy, w/o setting the mode
  FINE_GRAIN_LOG(info, "Build the logger for this file.");
  Logger::Context::enableFineGrainLogger();
  postCallback("/logging", header_map, response);
  FINE_GRAIN_LOG(error, response.toString());

  EXPECT_EQ(Http::Code::OK, postCallback("/logging?level=warning", header_map, response));
  FINE_GRAIN_LOG(warn, "After post 1: all level is warning now!");
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(__FILE__)->level(), spdlog::level::warn);

  std::string query = fmt::format("/logging?{}=info", __FILE__);
  postCallback(query, header_map, response);
  FINE_GRAIN_LOG(info, "After post 2: level for this file is info now!");
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(__FILE__)->level(), spdlog::level::info);

  // Test multiple log levels with invalid logger name
  const std::string file_not_exists = "xxxxxxxxxx_not_exists_xxxxxxxxxxx";
  query = fmt::format("/logging?paths={}:warning,{}:warning", __FILE__, file_not_exists);
  EXPECT_EQ(Http::Code::OK, postCallback(query, header_map, response));
  FINE_GRAIN_LOG(trace,
                 "After post 3: level should be changed if there is a match with an OK response.");
  EXPECT_THAT(response.toString(), HasSubstr("active loggers:\n"));
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(__FILE__)->level(), spdlog::level::warn);
  EXPECT_THAT(getFineGrainLogContext().getFineGrainLogEntry(file_not_exists), IsNull());

  // Test multiple log levels at once
  const std::string file = "xxxx_test_logger_file_xxxx";
  std::atomic<spdlog::logger*> logger;
  getFineGrainLogContext().initFineGrainLogger(file, logger);
  query = fmt::format("/logging?paths={}:trace,{}:trace", __FILE__, file);
  EXPECT_EQ(Http::Code::OK, postCallback(query, header_map, response));
  FINE_GRAIN_LOG(trace, "After post 4: level for this file is trace now!");
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(__FILE__)->level(), spdlog::level::trace);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(file)->level(), spdlog::level::trace);

  // You can't set the overall level and multiple levels at once at the same time.
  EXPECT_EQ(Http::Code::BadRequest, postCallback(query + "&level=info", header_map, response));

  // It's OK to set a path with a blank level -- that happens with HTML forms.
  EXPECT_EQ(Http::Code::OK, postCallback(query + "&level=", header_map, response));
  // Likewise it's OK to set the level even if there's a blank path.
  EXPECT_EQ(Http::Code::OK, postCallback("/logging?level=warning&paths=", header_map, response));
}

TEST_P(AdminInstanceTest, LogLevelDisplay) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  Logger::Context::enableFineGrainLogger();
  FINE_GRAIN_LOG(info, "Build the logger for this file.");
  EXPECT_EQ(Http::Code::OK, postCallback("/logging?level=warning", header_map, response));
  postCallback("/logging", header_map, response);
  FINE_GRAIN_LOG(error, response.toString());
  EXPECT_THAT(response.toString(), HasSubstr("  " __FILE__ ": warning"));
}

TEST_P(AdminInstanceTest, LogLevelFineGrainGlobSupport) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  // Enable fine grain logger right now.
  Logger::Context::enableFineGrainLogger();
  postCallback("/logging", header_map, response);
  FINE_GRAIN_LOG(error, response.toString());

  EXPECT_EQ(Http::Code::OK, postCallback("/logging?level=trace", header_map, response));
  FINE_GRAIN_LOG(warn, "After post /logging?level=trace, all level is trace now!");
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(__FILE__)->level(), spdlog::level::trace);

  std::string query = fmt::format("/logging?{}=info", "logs_handler_test");
  postCallback(query, header_map, response);
  FINE_GRAIN_LOG(info, "After post {}, level for this file is info now!", query);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(__FILE__)->level(), spdlog::level::info);

  // Test multiple log levels at once
  const std::string file_one = "admin/logs_handler_test_one.cc";
  const std::string file_two = "admin/logs_handler_test_two.cc";
  std::atomic<spdlog::logger*> logger_one;
  std::atomic<spdlog::logger*> logger_two;
  getFineGrainLogContext().initFineGrainLogger(file_one, logger_one);
  getFineGrainLogContext().initFineGrainLogger(file_two, logger_two);
  query = fmt::format("/logging?{}=critical", "logs_handle*");
  EXPECT_EQ(Http::Code::OK, postCallback(query, header_map, response));
  FINE_GRAIN_LOG(critical, "After post {}, level for this file is critical now!", query);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(__FILE__)->level(),
            spdlog::level::critical);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(file_one)->level(),
            spdlog::level::critical);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(file_two)->level(),
            spdlog::level::critical);

  query = fmt::format("/logging?paths={}:warning", "admin/*");
  EXPECT_EQ(Http::Code::OK, postCallback(query, header_map, response));
  FINE_GRAIN_LOG(trace, "After post {}, level for this file is trace (the default) now!", query);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(__FILE__)->level(), spdlog::level::trace);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(file_one)->level(), spdlog::level::warn);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(file_two)->level(), spdlog::level::warn);

  query = fmt::format("/logging?paths={}:info", "admin/logs_handler_test????.cc");
  EXPECT_EQ(Http::Code::OK, postCallback(query, header_map, response));
  FINE_GRAIN_LOG(trace, "After post {}, level for this file is still trace!", query);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(__FILE__)->level(), spdlog::level::trace);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(file_one)->level(), spdlog::level::info);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(file_two)->level(), spdlog::level::info);

  query = fmt::format("/logging?paths={}:warning", "*admin/logs_handler_test*");
  EXPECT_EQ(Http::Code::OK, postCallback(query, header_map, response));
  FINE_GRAIN_LOG(warn, "After post {}, level for this file is warn now!", query);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(__FILE__)->level(), spdlog::level::warn);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(file_one)->level(), spdlog::level::warn);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(file_two)->level(), spdlog::level::warn);

  // Only first glob match takes effect.
  query =
      fmt::format("/logging?paths={}:warning,{}:info", "logs_handler_test*", "logs_handler_test*");
  EXPECT_EQ(Http::Code::OK, postCallback(query, header_map, response));
  FINE_GRAIN_LOG(warn, "After post {}, level for this file is warn now!", query);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(__FILE__)->level(), spdlog::level::warn);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(file_one)->level(), spdlog::level::warn);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(file_two)->level(), spdlog::level::warn);

  // The first glob or base-name match takes effect.
  query = fmt::format("/logging?paths={}:warning,{}:info", "logs_handler_test_one",
                      "logs_handler_test*");
  EXPECT_EQ(Http::Code::OK, postCallback(query, header_map, response));
  FINE_GRAIN_LOG(info, "After post {}, level for this file is info now!", query);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(__FILE__)->level(), spdlog::level::info);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(file_one)->level(), spdlog::level::warn);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(file_two)->level(), spdlog::level::info);

  // The level of unmatched loggers will be the default.
  query = fmt::format("/logging?paths={}:critical", "logs_handler_test");
  EXPECT_EQ(Http::Code::OK, postCallback(query, header_map, response));
  FINE_GRAIN_LOG(info, "After post {}, level for this file is info now!", query);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(__FILE__)->level(),
            spdlog::level::critical);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(file_one)->level(), spdlog::level::trace);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(file_two)->level(), spdlog::level::trace);
}

} // namespace Server
} // namespace Envoy
