#include <string>

#include "source/common/runtime/breaking_changes.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/flags/flag.h"
#include "gtest/gtest.h"

namespace Envoy {

using ::testing::HasSubstr;
using ::testing::Not;

class Http1BreakingChangesTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public HttpIntegrationTest {
public:
  Http1BreakingChangesTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void TearDown() override { absl::SetFlag(&FLAGS_breaking_change_observability_enabled, false); }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, Http1BreakingChangesTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(Http1BreakingChangesTest, TestEnabledBreakingChange) {
  absl::SetFlag(&FLAGS_breaking_change_observability_enabled, true);
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

  ASSERT_NE(upstream_request_, nullptr);
  EXPECT_TRUE(upstream_request_->headers()
                  .get(Http::LowerCaseString("test_enabled_breaking_change"))
                  .empty());

  const std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("test_enabled_breaking_change"));
  EXPECT_THAT(log, Not(HasSubstr("test_disabled_breaking_change")));
}

TEST_P(Http1BreakingChangesTest, TestDisabledBreakingChange) {
  absl::SetFlag(&FLAGS_breaking_change_observability_enabled, true);
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

  ASSERT_NE(upstream_request_, nullptr);
  EXPECT_FALSE(upstream_request_->headers()
                   .get(Http::LowerCaseString("test_disabled_breaking_change"))
                   .empty());

  const std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("test_disabled_breaking_change"));
  EXPECT_THAT(log, Not(HasSubstr("test_enabled_breaking_change")));
}

TEST_P(Http1BreakingChangesTest,
       TestDisabledBreakingChangeEnabledManuallyAndObservabilityDisabled) {
  absl::SetFlag(&FLAGS_breaking_change_observability_enabled, false);
  config_helper_.addRuntimeOverride("envoy.reloadable_features.test_disabled_breaking_change",
                                    "true");
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

  ASSERT_NE(upstream_request_, nullptr);
  // Header is removed as the breaking change is enabled.
  EXPECT_TRUE(upstream_request_->headers()
                  .get(Http::LowerCaseString("test_disabled_breaking_change"))
                  .empty());

  const std::string log = waitForAccessLog(access_log_name_);
  // Since observability is disabled, there is nothing in the access log.
  EXPECT_THAT(log, Not(HasSubstr("test_disabled_breaking_change")));
  EXPECT_THAT(log, Not(HasSubstr("test_enabled_breaking_change")));
}

TEST_P(Http1BreakingChangesTest, ObservabilityDisabled) {
  absl::SetFlag(&FLAGS_breaking_change_observability_enabled, false);
  useAccessLog("%FILTER_STATE(envoy.breaking_changes_tracker:PLAIN)%");
  config_helper_.prependFilter(R"EOF(
name: breaking-change-filter
typed_config:
  "@type": type.googleapis.com/test.integration.filters.BreakingChangeFilterConfig
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.addCopy("test_enabled_breaking_change", "true");
  default_request_headers_.addCopy("test_disabled_breaking_change", "true");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  const std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, Not(HasSubstr("test_enabled_breaking_change")));
  EXPECT_THAT(log, Not(HasSubstr("test_disabled_breaking_change")));
}

TEST_P(Http1BreakingChangesTest, ObservabilityEnabledInBootstrap) {
  absl::SetFlag(&FLAGS_breaking_change_observability_enabled, false);
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.set_enable_breaking_changes_observability(true);
  });
  useAccessLog("%FILTER_STATE(envoy.breaking_changes_tracker:PLAIN)%");
  config_helper_.prependFilter(R"EOF(
name: breaking-change-filter
typed_config:
  "@type": type.googleapis.com/test.integration.filters.BreakingChangeFilterConfig
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.addCopy("test_enabled_breaking_change", "true");
  default_request_headers_.addCopy("test_disabled_breaking_change", "true");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  const std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("test_enabled_breaking_change"));
  EXPECT_THAT(log, HasSubstr("test_disabled_breaking_change"));
}

} // namespace Envoy
