#include "common/common/fancy_logger.h"
#include "common/common/logger.h"

#include "test/server/admin/admin_instance.h"

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
  FANCY_LOG(info, "Build the logger for this file.");
  Logger::Context::enableFancyLogger();
  postCallback("/logging", header_map, response);
  FANCY_LOG(error, response.toString());

  postCallback("/logging?level=warning", header_map, response);
  FANCY_LOG(warn, "After post 1: all level is warning now!");
  EXPECT_EQ(getFancyContext().getFancyLogEntry(__FILE__)->level(), spdlog::level::warn);
  std::string query = fmt::format("/logging?{}=info", __FILE__);
  postCallback(query, header_map, response);
  FANCY_LOG(info, "After post 2: level for this file is info now!");
  EXPECT_EQ(getFancyContext().getFancyLogEntry(__FILE__)->level(), spdlog::level::info);
}

} // namespace Server
} // namespace Envoy
