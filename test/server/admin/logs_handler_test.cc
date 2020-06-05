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

} // namespace Server
} // namespace Envoy
