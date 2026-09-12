#include <string>

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

class Http1BreakingChangesTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public HttpIntegrationTest {
public:
  Http1BreakingChangesTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, Http1BreakingChangesTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(Http1BreakingChangesTest, TestEnabledBreakingChange) {
  useAccessLog("%FILTER_STATE(envoy.breaking_changes_tracker:PLAIN)%");
  config_helper_.prependFilter(R"EOF(
name: breaking-change-filter
typed_config:
  "@type": type.googleapis.com/test.integration.filters.BreakingChangeFilterConfig
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.addCopy("test_enabled_breaking_change", "true");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  const std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, testing::HasSubstr("test_enabled_breaking_change"));
  EXPECT_THAT(log, testing::Not(testing::HasSubstr("test_disabled_breaking_change")));

  cleanupUpstreamAndDownstream();
}

TEST_P(Http1BreakingChangesTest, TestDisabledBreakingChange) {
  useAccessLog("%FILTER_STATE(envoy.breaking_changes_tracker:PLAIN)%");
  config_helper_.prependFilter(R"EOF(
name: breaking-change-filter
typed_config:
  "@type": type.googleapis.com/test.integration.filters.BreakingChangeFilterConfig
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.addCopy("test_disabled_breaking_change", "true");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  const std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, testing::HasSubstr("test_disabled_breaking_change"));
  EXPECT_THAT(log, testing::Not(testing::HasSubstr("test_enabled_breaking_change")));

  cleanupUpstreamAndDownstream();
}

} // namespace Envoy
