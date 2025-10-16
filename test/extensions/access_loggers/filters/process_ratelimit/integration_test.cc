#include "envoy/config/accesslog/v3/accesslog.pb.validate.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/access_loggers/filters/process_ratelimit/v3/process_ratelimit.pb.h"
#include "envoy/type/v3/token_bucket.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using testing::HasSubstr;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace ProcessRateLimit {
namespace {

class AccessLogIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public HttpIntegrationTest {
public:
  AccessLogIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AccessLogIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

envoy::config::accesslog::v3::AccessLog parseAccessLogFromV3Yaml(const std::string& yaml) {
  envoy::config::accesslog::v3::AccessLog access_log;
  TestUtility::loadFromYamlAndValidate(yaml, access_log);
  return access_log;
}

TEST_P(AccessLogIntegrationTest, AccessLogLocalRateLimitFilter) {
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
    "@type": type.googleapis.com/envoy.type.v3.TokenBucket
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
      "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.process_ratelimit.v3.ProcessRateLimitFilter
      dynamic_config:
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

  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  cleanupUpstreamAndDownstream();

  auto entries = waitForAccessLogEntries(access_log_path, nullptr);
  // We have 4 access logs triggered but 1 got rate limited.
  EXPECT_EQ(3, entries.size());

  timeSystem().advanceTimeWait(std::chrono::seconds(2));

  codec_client_ = makeHttpConnection(lookupPort("http"));

  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  cleanupUpstreamAndDownstream();
  entries = waitForAccessLogEntries(access_log_path, nullptr);
  // We have another 4 access logs triggered but 1 got rate limited.
  EXPECT_EQ(6, entries.size());
}

} // namespace
} // namespace ProcessRateLimit
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
