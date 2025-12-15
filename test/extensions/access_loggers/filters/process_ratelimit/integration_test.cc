#include "envoy/config/accesslog/v3/accesslog.pb.validate.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/access_loggers/filters/process_ratelimit/v3/process_ratelimit.pb.h"
#include "envoy/type/v3/token_bucket.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "test/integration/ads_integration.h"
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

envoy::config::accesslog::v3::AccessLog parseAccessLogFromV3Yaml(const std::string& yaml) {
  envoy::config::accesslog::v3::AccessLog access_log;
  TestUtility::loadFromYamlAndValidate(yaml, access_log);
  return access_log;
}

class AccessLogIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public HttpIntegrationTest {
public:
  AccessLogIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AccessLogIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

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

class AccessLogAdsIntegrationTest : public AdsIntegrationTest {
public:
  void SetUp() override {
    initialize();

    // Initial ADS setup: cluster, listener and route configuration.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "", {}, {}, {}, true));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        Config::TestTypeUrl::get().Cluster, {buildCluster("cluster_0")},
        {buildCluster("cluster_0")}, {}, "1");

    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().ClusterLoadAssignment, "",
                                        {"cluster_0"}, {"cluster_0"}, {}));
    sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
        Config::TestTypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
        {buildClusterLoadAssignment("cluster_0")}, {}, "1");

    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "1", {}, {}, {}));
    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Listener, "", {}, {}, {}));
    sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
        Config::TestTypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
        {buildListener("listener_0", "route_config_0")}, {}, "1");

    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().ClusterLoadAssignment, "1",
                                        {"cluster_0"}, {}, {}));
    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().RouteConfiguration, "",
                                        {"route_config_0"}, {"route_config_0"}, {}));
    sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
        Config::TestTypeUrl::get().RouteConfiguration,
        {buildRouteConfig("route_config_0", "cluster_0")},
        {buildRouteConfig("route_config_0", "cluster_0")}, {}, "1");

    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Listener, "1", {}, {}, {}));
    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().RouteConfiguration, "1",
                                        {"route_config_0"}, {}, {}));

    test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);

    // Make a request to verify listener_0 is working.
    makeSingleRequest();
  }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersions, AccessLogAdsIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),
                     testing::Values(Grpc::SotwOrDelta::Delta)),
    AccessLogAdsIntegrationTest::protocolTestParamsToString);

TEST_P(AccessLogAdsIntegrationTest, AccessLogLocalRateLimitFilterAds) {
  // Prepare listener_1 with one access logger using a rate-limiting
  // filter with ADS-based dynamic configuration for the token bucket.
  const std::string access_log_path = TestEnvironment::temporaryPath(fmt::format(
      "access_log_{}_{}.txt", ipVersion() == Network::Address::IpVersion::v4 ? "v4" : "v6",
      TestUtility::uniqueFilename()));
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
          ads: {{}}
          resource_api_version: V3
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
  path: "{}"
)EOF",
                                                  access_log_path);
  envoy::config::listener::v3::Listener listener_1 = buildListener("listener_1", "route_config_0");
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
      hcm_config;
  listener_1.filter_chains(0).filters(0).typed_config().UnpackTo(&hcm_config);
  *hcm_config.add_access_log() = parseAccessLogFromV3Yaml(access_log_yaml);
  listener_1.mutable_filter_chains(0)->mutable_filters(0)->mutable_typed_config()->PackFrom(
      hcm_config);

  // Update listeners: remove listener_0 and add listener_1.
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TestTypeUrl::get().Listener, {listener_1}, {listener_1}, {"listener_0"}, "2");

  // Envoy should request token bucket configuration via ADS for listener_1.
  const std::string token_bucket_type_url = "type.googleapis.com/envoy.type.v3.TokenBucket";
  EXPECT_TRUE(compareDiscoveryRequest(token_bucket_type_url, "", {"token_bucket_name"},
                                      {"token_bucket_name"}, {}));

  // Build and send token bucket configuration: 3 max tokens, 1 token/sec refill rate.
  envoy::type::v3::TokenBucket bucket;
  bucket.set_max_tokens(3);
  bucket.mutable_tokens_per_fill()->set_value(1);
  bucket.mutable_fill_interval()->set_seconds(1);

  absl::flat_hash_map<std::string, envoy::type::v3::TokenBucket> token_buckets;
  token_buckets.emplace("token_bucket_name", bucket);
  sendMapDiscoveryResponse<envoy::type::v3::TokenBucket>(token_bucket_type_url, token_buckets,
                                                         token_buckets, {}, "1");
  // Envoy should ACK listener update version 2, and token bucket config version 1.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Listener, "2", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(token_bucket_type_url, "1", {"token_bucket_name"}, {}, {}));

  // Recreate connection and send 4 requests. Each request triggers 1 access log.
  cleanupUpstreamAndDownstream();
  registerTestServerPorts({"http"});
  codec_client_ = makeHttpConnection(lookupPort("http"));
  for (int i = 0; i < 4; i++) {
    sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  }
  cleanupUpstreamAndDownstream();

  auto entries = waitForAccessLogEntries(access_log_path, nullptr);
  // We have 4 access logs triggered but 1 got rate limited by token bucket(max_tokens=3).
  EXPECT_EQ(3, entries.size());

  // Advance time to allow token bucket to refill.
  timeSystem().advanceTimeWait(std::chrono::seconds(3));

  // Add listener_2 with the same config.
  envoy::config::listener::v3::Listener listener_2 = listener_1;
  listener_2.set_name("listener_2");
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TestTypeUrl::get().Listener, {listener_1, listener_2}, {listener_2}, {}, "3");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Listener, "3", {}, {}, {}));

  // Send requests to both listeners.
  cleanupUpstreamAndDownstream();
  registerTestServerPorts({"http", "http2"});
  codec_client_ = makeHttpConnection(lookupPort("http"));
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  cleanupUpstreamAndDownstream();

  codec_client_ = makeHttpConnection(lookupPort("http2"));
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  cleanupUpstreamAndDownstream();

  entries = waitForAccessLogEntries(access_log_path, nullptr);
  // After refill, we expect another 3 access logs to be written, and 1 to be rate limited.
  // 3 from previous step + 3 from this step = 6.
  EXPECT_EQ(6, entries.size());

  // Advance time to allow token bucket to refill.
  timeSystem().advanceTimeWait(std::chrono::seconds(3));

  // Remove listener_1.
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TestTypeUrl::get().Listener, {listener_2}, {}, {"listener_1"}, "4");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Listener, "4", {}, {}, {}));

  // Send 4 requests to listener_2.
  cleanupUpstreamAndDownstream();
  registerTestServerPorts({"http"});
  codec_client_ = makeHttpConnection(lookupPort("http"));
  for (int i = 0; i < 4; i++) {
    sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  }
  cleanupUpstreamAndDownstream();
  entries = waitForAccessLogEntries(access_log_path, nullptr);
  // 3 from step 1 + 3 from step 2 + 3 from step 3 = 9.
  EXPECT_EQ(9, entries.size());
}

} // namespace
} // namespace ProcessRateLimit
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
