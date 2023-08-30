#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::HasSubstr;

namespace Envoy {

class AccessLogIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public HttpIntegrationTest {
public:
  AccessLogIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AccessLogIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AccessLogIntegrationTest, DownstreamDisconnectBeforeHeadersResponseCode) {
  useAccessLog("RESPONSE_CODE=%RESPONSE_CODE%");
  testRouterDownstreamDisconnectBeforeRequestComplete();
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("RESPONSE_CODE=0"));
}
} // namespace Envoy
