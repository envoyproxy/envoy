#include "envoy/config/metrics/v3/scope.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/ads_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class StatsAccessLogIntegrationTest : public HttpIntegrationTest,
                                      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  StatsAccessLogIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void init(const std::string& config_yaml, bool autonomous_upstream = true,
            bool flush_access_log_on_new_request = false) {
    init(std::vector<std::string>{config_yaml}, autonomous_upstream,
         flush_access_log_on_new_request);
  }

  void init(const std::vector<std::string>& config_yamls, bool autonomous_upstream = true,
            bool flush_access_log_on_new_request = false) {
    autonomous_upstream_ = autonomous_upstream;
    config_helper_.addConfigModifier(
        [&, config_yamls](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          if (flush_access_log_on_new_request) {
            hcm.mutable_access_log_options()->set_flush_access_log_on_new_request(true);
          }
          for (const auto& config_yaml : config_yamls) {
            auto* access_log = hcm.add_access_log();
            TestUtility::loadFromYaml(config_yaml, *access_log);
          }
        });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, StatsAccessLogIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(StatsAccessLogIntegrationTest, Basic) {
  const std::string config_yaml = R"EOF(
              name: envoy.access_loggers.stats
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stat_prefix: test_stat_prefix
                counters:
                  - stat:
                      name: fixedcounter
                      tags:
                        - name: fixed_tag
                          value_format: fixed_value
                        - name: dynamic_tag
                          value_format: '%REQUEST_HEADER(tag-value)%_%PROTOCOL%'
                    value_fixed: 42
                  - stat:
                      name: formatcounter
                    value_format: '%RESPONSE_CODE%'
                histograms:
                  - stat:
                      name: testhistogram
                      tags:
                        - name: tag
                          value_format: '%REQUEST_HEADER(tag-value)%'
                    value_format: '%REQUEST_HEADER(histogram-value)%'

)EOF";

  init(config_yaml);

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},       {":authority", "envoyproxy.io"}, {":path", "/test/long/url"},
      {":scheme", "http"},      {"tag-value", "mytagvalue"},     {"counter-value", "7"},
      {"histogram-value", "2"},
  };

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  test_server_->waitForCounterEq(
      "test_stat_prefix.fixedcounter.fixed_tag.fixed_value.dynamic_tag.mytagvalue_HTTP/1.1", 42);
  test_server_->waitForCounterEq("test_stat_prefix.formatcounter", 200);
  test_server_->waitUntilHistogramHasSamples("test_stat_prefix.testhistogram.tag.mytagvalue");

  auto histogram = test_server_->histogram("test_stat_prefix.testhistogram.tag.mytagvalue");
  EXPECT_EQ(1, TestUtility::readSampleCount(test_server_->server().dispatcher(), *histogram));
  EXPECT_EQ(2, static_cast<uint32_t>(
                   TestUtility::readSampleSum(test_server_->server().dispatcher(), *histogram)));
}

// Trigger simultaneous logs on multiple workers to trigger TSAN errors if present.
TEST_P(StatsAccessLogIntegrationTest, Concurrency) {
  concurrency_ = 2;
  const std::string config_yaml = R"EOF(
              name: envoy.access_loggers.stats
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stat_prefix: test_stat_prefix
                counters:
                  - stat:
                      name: formatcounter
                    value_format: '%RESPONSE_CODE%'
                histograms:
                  - stat:
                      name: testhistogram
                      tags:
                        - name: tag
                          value_format: '%REQUEST_HEADER(tag-value)%'
                    value_format: '%REQUEST_HEADER(histogram-value)%'

)EOF";

  init(config_yaml);

  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},       {":authority", "envoyproxy.io"}, {":path", "/test/long/url"},
      {":scheme", "http"},      {"tag-value", "mytagvalue"},     {"counter-value", "7"},
      {"histogram-value", "2"},
  };

  std::vector<std::thread> threads;
  for (uint32_t i = 0; i < 10; i++) {
    threads.emplace_back([&]() {
      for (uint32_t requests = 0; requests < 10; requests++) {
        BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
            lookupPort("http"), "GET", "/", "", downstream_protocol_, version_, "envoyproxy.io");
        ASSERT_TRUE(response->complete());
        EXPECT_EQ("200", response->headers().getStatusValue());
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }
}

TEST_P(StatsAccessLogIntegrationTest, PercentHistogram) {
  const std::string config_yaml = R"EOF(
              name: envoy.access_loggers.stats
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stat_prefix: test_stat_prefix
                histograms:
                  - stat:
                      name: testhistogram
                    unit: Percent
                    value_format: '%REQUEST_HEADER(histogram-value)%'

)EOF";

  init(config_yaml);

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},  {":authority", "envoyproxy.io"}, {":path", "/test/long/url"},
      {":scheme", "http"}, {"histogram-value", "0.1"},
  };

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  test_server_->waitUntilHistogramHasSamples("test_stat_prefix.testhistogram");

  auto histogram = test_server_->histogram("test_stat_prefix.testhistogram");
  EXPECT_EQ(1, TestUtility::readSampleCount(test_server_->server().dispatcher(), *histogram));

  double p100 = histogram->cumulativeStatistics().computedQuantiles().back();
  EXPECT_NEAR(0.1, p100, 0.05);
}

TEST_P(StatsAccessLogIntegrationTest, ActiveRequestsGauge) {
  const std::string config_yaml = R"EOF(
              name: envoy.access_loggers.stats
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stat_prefix: test_stat_prefix
                gauges:
                  - stat:
                      name: active_requests
                      tags:
                        - name: request_header_tag
                          value_format: '%REQUEST_HEADER(tag-value)%'
                    value_fixed: 1
                    add_subtract:
                      add_log_type: DownstreamStart
                      sub_log_type: DownstreamEnd
)EOF";

  init(config_yaml, /*autonomous_upstream=*/false,
       /*flush_access_log_on_new_request=*/true);

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},  {":authority", "envoyproxy.io"}, {":path", "/"},
      {":scheme", "http"}, {"tag-value", "my-tag"},
  };

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  // Wait for upstream to receive request.
  waitForNextUpstreamRequest();

  // After DownstreamStart is logged, gauge should be 1.
  test_server_->waitForGaugeEq("test_stat_prefix.active_requests.request_header_tag.my-tag", 1);

  // Send response from upstream.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);

  // Wait for client to receive response.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  // After DownstreamEnd is logged, gauge should be 0.
  test_server_->waitForGaugeEq("test_stat_prefix.active_requests.request_header_tag.my-tag", 0);
}

TEST_P(StatsAccessLogIntegrationTest, SubtractWithoutAdd) {
  const std::string config_yaml = R"EOF(
              name: envoy.access_loggers.stats
              filter:
                log_type_filter:
                  types: [DownstreamEnd]
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stat_prefix: test_stat_prefix
                gauges:
                  - stat:
                      name: active_requests
                      tags:
                        - name: request_header_tag
                          value_format: '%REQUEST_HEADER(tag-value)%'
                    value_fixed: 1
                    add_subtract:
                      add_log_type: DownstreamStart
                      sub_log_type: DownstreamEnd
)EOF";

  init(config_yaml, /*autonomous_upstream=*/false,
       /*flush_access_log_on_new_request=*/true);

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},  {":authority", "envoyproxy.io"}, {":path", "/"},
      {":scheme", "http"}, {"tag-value", "my-tag"},
  };

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  // Wait for upstream to receive request.
  waitForNextUpstreamRequest();

  // Since DownstreamStart is filtered out, gauge should be 0.
  // Note: waitForGaugeEq waits for the gauge to exist and equal the value.
  // If no stats are emitted yet, it might timeout or fail depending on implementation.
  // However, in this case, we expect NO stats to be emitted at start.
  // We can't verify "stat doesn't exist" easily with waitForGaugeEq.
  // But we proceed.

  // Send response from upstream.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);

  // Wait for client to receive response.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  // After DownstreamEnd is logged, subtract should be skipped because Add didn't happen.
  // Gauge should still be 0.
  test_server_->waitForGaugeEq("test_stat_prefix.active_requests.request_header_tag.my-tag", 0);
}

TEST_P(StatsAccessLogIntegrationTest, FileBasedDynamicScope) {
  const std::string dynamic_scope_yaml =
      TestEnvironment::writeStringToFileForTest("dynamic_scope.yaml", R"EOF(
resources:
  - "@type": "type.googleapis.com/envoy.service.discovery.v3.Resource"
    name: my_dynamic_scope
    resource:
      "@type": "type.googleapis.com/envoy.config.metrics.v3.Scope"
      max_counters: 2
)EOF");

  const std::string logger1_yaml = fmt::format(R"EOF(
              name: envoy.access_loggers.stats
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stat_prefix: test_stat_prefix
                dynamic_scope:
                  resource_name: my_dynamic_scope
                  config_source:
                    resource_api_version: V3
                    path_config_source:
                      path: {}
                counters:
                  - stat:
                      name: counter1
                    value_fixed: 1
)EOF",
                                               dynamic_scope_yaml);

  const std::string logger2_yaml = fmt::format(R"EOF(
              name: envoy.access_loggers.stats
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stat_prefix: test_stat_prefix
                dynamic_scope:
                  resource_name: my_dynamic_scope
                  config_source:
                    resource_api_version: V3
                    path_config_source:
                      path: {}
                counters:
                  - stat:
                      name: counter2
                    value_fixed: 2
                  - stat:
                      name: counter3
                    value_fixed: 3
)EOF",
                                               dynamic_scope_yaml);

  init({logger1_yaml, logger2_yaml});

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":authority", "envoyproxy.io"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
  };

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  // Two loggers share the same scope "my_dynamic_scope" with max_counters=2.
  // logger1 creates counter1.
  // logger2 creates counter2 and counter3.
  // counter1 and counter2 should be created.
  test_server_->waitForCounterEq("my_dynamic_scope.counter1", 1);
  test_server_->waitForCounterEq("my_dynamic_scope.counter2", 2);
  // counter3 should not be created because the limit is reached.
  EXPECT_EQ(nullptr,
            TestUtility::findCounter(test_server_->statStore(), "my_dynamic_scope.counter3"));
}

class StatsAccessLogAdsIntegrationTest : public AdsIntegrationTest {
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
    IpVersions, StatsAccessLogAdsIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),
                     testing::Values(Grpc::SotwOrDelta::Delta)),
    StatsAccessLogAdsIntegrationTest::protocolTestParamsToString);

TEST_P(StatsAccessLogAdsIntegrationTest, DynamicScopeAds) {
  // Prepare listener_1 with one access logger using dynamic scope configuration.
  const std::string access_log_yaml = R"EOF(
name: envoy.access_loggers.stats
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
  stat_prefix: test_stat_prefix
  dynamic_scope:
    resource_name: my_dynamic_scope
    config_source:
      ads: {}
      resource_api_version: V3
  counters:
    - stat:
        name: counter1
      value_fixed: 1
    - stat:
        name: counter2
      value_fixed: 1
)EOF";

  envoy::config::listener::v3::Listener listener_1 = buildListener("listener_1", "route_config_0");
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
      hcm_config;
  listener_1.filter_chains(0).filters(0).typed_config().UnpackTo(&hcm_config);
  auto* access_log = hcm_config.add_access_log();
  TestUtility::loadFromYaml(access_log_yaml, *access_log);
  listener_1.mutable_filter_chains(0)->mutable_filters(0)->mutable_typed_config()->PackFrom(
      hcm_config);

  // Update listeners: remove listener_0 and add listener_1.
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TestTypeUrl::get().Listener, {listener_1}, {listener_1}, {"listener_0"}, "2");

  // Envoy should request scope configuration via ADS for listener_1.
  const std::string scope_type_url = "type.googleapis.com/envoy.config.metrics.v3.Scope";
  EXPECT_TRUE(
      compareDiscoveryRequest(scope_type_url, "", {"my_dynamic_scope"}, {"my_dynamic_scope"}, {}));

  // Build and send scope configuration with limit 1.
  envoy::config::metrics::v3::Scope scope;
  scope.set_max_counters(1);
  absl::flat_hash_map<std::string, envoy::config::metrics::v3::Scope> scopes;
  scopes.emplace("my_dynamic_scope", scope);
  sendMapDiscoveryResponse<envoy::config::metrics::v3::Scope>(scope_type_url, scopes, scopes, {},
                                                              "1");
  // Envoy should ACK listener update version 2, and scope config version 1.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Listener, "2", {}, {}, {}));
  EXPECT_TRUE(compareDiscoveryRequest(scope_type_url, "1", {"my_dynamic_scope"}, {}, {}));

  // Recreate connection and send a request.
  cleanupUpstreamAndDownstream();
  registerTestServerPorts({"http"});
  codec_client_ = makeHttpConnection(lookupPort("http"));
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);

  test_server_->waitForCounterEq("my_dynamic_scope.counter1", 1);

  // Verify counter2 is NOT created due to limit.
  EXPECT_EQ(nullptr,
            TestUtility::findCounter(test_server_->statStore(), "my_dynamic_scope.counter2"));

  // Increase limit to 2.
  scope.set_max_counters(2);
  scopes["my_dynamic_scope"] = scope;
  sendMapDiscoveryResponse<envoy::config::metrics::v3::Scope>(scope_type_url, scopes, scopes, {},
                                                              "2");
  EXPECT_TRUE(compareDiscoveryRequest(scope_type_url, "2", {"my_dynamic_scope"}, {}, {}));

  // Send another request.
  cleanupUpstreamAndDownstream();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);

  // Now both counters should be present and updated.
  // Note: Scope update via ADS might cause stats reset, so we expect counter1 to be 1 (from new
  // request) rather than 2.
  test_server_->waitForCounterEq("my_dynamic_scope.counter1", 1);
  test_server_->waitForCounterEq("my_dynamic_scope.counter2", 1);
}

} // namespace
} // namespace Envoy
