#include "envoy/config/accesslog/v3/accesslog.pb.validate.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/type/v3/token_bucket.pb.validate.h"

#include "source/common/protobuf/protobuf.h"

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
  useAccessLog("RESPONSE_CODE=%RESPONSE_CODE%;CEL_METHOD=%CEL(request.headers[':method'])%");
  testRouterDownstreamDisconnectBeforeRequestComplete();
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("RESPONSE_CODE=0;CEL_METHOD=GET"));
}

TEST_P(AccessLogIntegrationTest, ShouldReplaceInvalidUtf8) {
  // Add incomplete UTF-8 strings.
  default_request_headers_.setForwardedFor("\xec");

  // Update access logs to use json format sorted.
  access_log_name_ = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  config_helper_.addConfigModifier(
      [this](
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
        auto* access_log_config_to_clobber = hcm.add_access_log();
        access_log_config.set_path(access_log_name_);

        auto* log_format = access_log_config.mutable_log_format();
        auto* json = log_format->mutable_json_format();
        Envoy::Protobuf::Value v;
        v.set_string_value("%REQ(X-FORWARDED-FOR)%");
        auto fields = json->mutable_fields();
        (*fields)["x_forwarded_for"] = v;
        access_log_config_to_clobber->mutable_typed_config()->PackFrom(access_log_config);
      });
  testRouterDownstreamDisconnectBeforeRequestComplete();
  const std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("x_forwarded_for\":\"\\u00ec"));
}

envoy::config::accesslog::v3::AccessLog parseAccessLogFromV3Yaml(const std::string& yaml) {
  envoy::config::accesslog::v3::AccessLog access_log;
  TestUtility::loadFromYamlAndValidate(yaml, access_log);
  return access_log;
}

// Test that the LocalRateLimiterFilter for access log works.
TEST_P(AccessLogIntegrationTest, AccessLogLocalRateLimitFilter) {
  // Prepare the TokenBucket config file

  const std::string token_bucket_path = TestEnvironment::temporaryPath(fmt::format(
      "token_bucket_{}_{}.yaml", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6",
      TestUtility::uniqueFilename()));
  TestEnvironment::writeStringToFileForTest(token_bucket_path, R"EOF(
version_info: "123"
resources:
- "@type": type.googleapis.com/envoy.service.discovery.v3.Resource
  name: "token_bucket_name"
  version: "100"
  resource:
    "@type": type.googleapis.com/envoy.type.v3.TokenBucketConfig
    name: "token_bucket_name"
    token_bucket:
      max_tokens: 3
      tokens_per_fill: 1
      fill_interval:
        seconds: 1
)EOF",
                                            true);

  const std::string access_log_path = TestEnvironment::temporaryPath(
      fmt::format("access_log_{}_{}.txt", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6",
                  TestUtility::uniqueFilename()));

  config_helper_.addConfigModifier(
      [token_bucket_path, access_log_path](
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        const std::string access_log_yaml = fmt::format(R"EOF(
name: accesslog
filter:
  extension_filter:
    name: local_ratelimit_extension_filter
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.local_ratelimit.v3.LocalRateLimitFilter
      resource_name: "token_bucket_name"
      config_source:
        path_config_source:
          path: "{}"
        resource_api_version: V3
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
  path: "{}"
)EOF",
                                                        token_bucket_path, access_log_path);
        auto* access_log1 = hcm.add_access_log();
        *access_log1 = parseAccessLogFromV3Yaml(access_log_yaml);
        auto* access_log2 = hcm.add_access_log();
        *access_log2 = parseAccessLogFromV3Yaml(access_log_yaml);
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send 2 requests, only the first one should be logged.
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  cleanupUpstreamAndDownstream();

  auto entries = waitForAccessLogEntries(access_log_path, nullptr);
  EXPECT_EQ(3, entries.size());

  // Advance the time by 1 second, so the token bucket will be refilled.
  timeSystem().advanceTimeWait(std::chrono::seconds(2));

  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Send 2 requests, both should be logged.
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  cleanupUpstreamAndDownstream();
  entries = waitForAccessLogEntries(access_log_path, nullptr);
  EXPECT_EQ(6, entries.size());
}
} // namespace Envoy
