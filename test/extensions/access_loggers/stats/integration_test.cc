#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/ads_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Eq;
using testing::Ge;
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
            envoy::config::accesslog::v3::AccessLog* access_log = hcm.add_access_log();
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
                stats_scope:
                  prefix: test_stat_prefix
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
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  test_server_->waitForCounter(
      "test_stat_prefix.fixedcounter.fixed_tag.fixed_value.dynamic_tag.mytagvalue_HTTP/1.1",
      Eq(42));
  test_server_->waitForCounter("test_stat_prefix.formatcounter", Eq(200));
  test_server_->waitUntilHistogramHasSamples("test_stat_prefix.testhistogram.tag.mytagvalue");

  Stats::ParentHistogramSharedPtr histogram =
      test_server_->histogram("test_stat_prefix.testhistogram.tag.mytagvalue");
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
                stats_scope:
                  prefix: test_stat_prefix
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
                stats_scope:
                  prefix: test_stat_prefix
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
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  test_server_->waitUntilHistogramHasSamples("test_stat_prefix.testhistogram");

  Stats::ParentHistogramSharedPtr histogram =
      test_server_->histogram("test_stat_prefix.testhistogram");
  EXPECT_EQ(1, TestUtility::readSampleCount(test_server_->server().dispatcher(), *histogram));

  double p100 = histogram->cumulativeStatistics().computedQuantiles().back();
  EXPECT_NEAR(0.1, p100, 0.05);
}

TEST_P(StatsAccessLogIntegrationTest, ActiveRequestsGauge) {
  const std::string config_yaml = R"EOF(
              name: envoy.access_loggers.stats
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stats_scope:
                  prefix: test_stat_prefix
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
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  // Wait for upstream to receive request.
  waitForNextUpstreamRequest();

  // After DownstreamStart is logged, gauge should be 1.
  test_server_->waitForGauge("test_stat_prefix.active_requests.request_header_tag.my-tag", Eq(1));

  // Send response from upstream.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);

  // Wait for client to receive response.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  // After DownstreamEnd is logged, gauge should be 0.
  test_server_->waitForGauge("test_stat_prefix.active_requests.request_header_tag.my-tag", Eq(0));

  codec_client_->close();
}

TEST_P(StatsAccessLogIntegrationTest, SubtractWithoutAdd) {
  const std::string config_yaml = R"EOF(
              name: envoy.access_loggers.stats
              filter:
                log_type_filter:
                  types: [DownstreamEnd]
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stats_scope:
                  prefix: test_stat_prefix
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

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},  {":authority", "envoyproxy.io"}, {":path", "/"},
      {":scheme", "http"}, {"tag-value", "my-tag"},
  };

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

  init(config_yaml, /*autonomous_upstream=*/false,
       /*flush_access_log_on_new_request=*/true);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(response_headers, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  test_server_->waitForGauge("test_stat_prefix.active_requests.request_header_tag.my-tag", Eq(0));
  codec_client_->close();
  test_server_->waitForCounter("http.config_test.downstream_cx_destroy", Ge(1));
}

TEST_P(StatsAccessLogIntegrationTest, GaugeInterleavedOpsWithEviction) {
  const std::string config_yaml = R"(
              name: envoy.access_loggers.stats
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stats_scope:
                  prefix: test_stat_prefix
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
)";

  init(config_yaml, /*autonomous_upstream=*/false,
       /*flush_access_log_on_new_request=*/true);

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},  {":authority", "envoyproxy.io"},       {":path", "/"},
      {":scheme", "http"}, {"tag-value", "my-eviction-test-tag"},
  };

  // Request 1: starts gauge at 1.
  IntegrationCodecClientPtr codec_client1 = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response1 = codec_client1->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();
  test_server_->waitForGauge(
      "test_stat_prefix.active_requests.request_header_tag.my-eviction-test-tag", Eq(1));

  // Simulate eviction from the store.
  absl::Notification evict_done;
  test_server_->server().dispatcher().post([this, &evict_done]() {
    // Two calls are required: the first to mark the stat as unused, and the second to actually
    // evict it from the store.
    test_server_->statStore().evictUnused();
    test_server_->statStore().evictUnused();
    evict_done.Notify();
  });
  evict_done.WaitForNotification();

  // Request 2: starts another concurrent request using the same tag.
  IntegrationCodecClientPtr codec_client2 = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response2 = codec_client2->makeHeaderOnlyRequest(request_headers);

  // Wait for the second request to reach upstream.
  // We need to keep track of the second upstream request.
  FakeStreamPtr upstream_request2;
  FakeHttpConnectionPtr fake_upstream_connection2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection2));
  ASSERT_TRUE(fake_upstream_connection2->waitForNewStream(*dispatcher_, upstream_request2));
  ASSERT_TRUE(upstream_request2->waitForEndStream(*dispatcher_));

  // The gauge should be kept even with eviction happened and the active request is 2.
  test_server_->waitForGauge(
      "test_stat_prefix.active_requests.request_header_tag.my-eviction-test-tag", Eq(2));

  // Clean up.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);
  ASSERT_TRUE(response1->waitForEndStream());
  test_server_->waitForGauge(
      "test_stat_prefix.active_requests.request_header_tag.my-eviction-test-tag", Eq(1));
  upstream_request2->encodeHeaders(response_headers, true);
  ASSERT_TRUE(response2->waitForEndStream());

  // Transition the state from used to unused, but not evicted.
  absl::Notification evict_done3;
  test_server_->server().dispatcher().post([this, &evict_done3]() {
    // Transition the state from used to unused, but not evicted yet.
    test_server_->statStore().evictUnused();
    evict_done3.Notify();
  });
  evict_done3.WaitForNotification();

  test_server_->waitForGauge(
      "test_stat_prefix.active_requests.request_header_tag.my-eviction-test-tag", Eq(0));

  codec_client1->close();
  codec_client2->close();
}

// This test verifies that if the gauge is evicted while the request is in-flight,
// the access logger can successfully recreate it when the request ends.
// In reality, it shouldn't happen because the gauge is protected from eviction while
// in-flight(value > 0).
TEST_P(StatsAccessLogIntegrationTest, ActiveRequestsGaugeEvictedWhileInflight) {
  const std::string config_yaml = R"(
              name: envoy.access_loggers.stats
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stats_scope:
                  prefix: test_stat_prefix
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
)";

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},  {":authority", "envoyproxy.io"},     {":path", "/"},
      {":scheme", "http"}, {"tag-value", "my-evict-crash-tag"},
  };

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

  // When the gauge is evicted after ADD but before SUB, the access logger can successfully recreate
  // it when the request ends. A newly recreated gauge starts at 0, so subtracting from it causes a
  // subtraction underflow warning which is expected to trigger EXPECT_DEBUG_DEATH.
  // Note: `EXPECT_DEBUG_DEATH` forks a subprocess. Forked processes do not inherit background
  // threads. Since Envoy integration tests rely on background server threads (started by `init`),
  // the setup must happen *inside* the block so that the threads are created within the subprocess.
  EXPECT_DEBUG_DEATH(
      {
        init(config_yaml, /*autonomous_upstream=*/false,
             /*flush_access_log_on_new_request=*/true);

        // Start request.
        IntegrationCodecClientPtr codec_client1 = makeHttpConnection(lookupPort("http"));
        IntegrationStreamDecoderPtr response1 =
            codec_client1->makeHeaderOnlyRequest(request_headers);
        waitForNextUpstreamRequest();
        test_server_->waitForGauge(
            "test_stat_prefix.active_requests.request_header_tag.my-evict-crash-tag", Eq(1));

        // Force gauge value to 0 so it can be evicted while FilterState is holding it.
        test_server_
            ->gauge("test_stat_prefix.active_requests.request_header_tag.my-evict-crash-tag")
            ->set(0);

        // Simulate eviction from the store.
        absl::Notification evict_done;
        test_server_->server().dispatcher().post([this, &evict_done]() {
          // Two calls are required: the first to mark the stat as unused, and the second to
          // actually evict it from the store.
          test_server_->statStore().evictUnused();
          test_server_->statStore().evictUnused();
          evict_done.Notify();
        });
        evict_done.WaitForNotification();

        upstream_request_->encodeHeaders(response_headers, true);
        ASSERT_TRUE(response1->waitForEndStream());

        codec_client1->close();
      },
      // This text check ensures the crash is from the underflow assert, not due to
      // crashing because of bad stat access in the StatsAccessLog. This text check is the entire
      // point of the test.
      "child_value_ >= amount");
}

TEST_P(StatsAccessLogIntegrationTest, GaugeCleanupOnDestructor) {
  const std::string config_yaml = R"(
              name: envoy.access_loggers.stats
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stats_scope:
                  prefix: test_stat_prefix
                gauges:
                  - stat:
                      name: active_requests
                      tags:
                        - name: request_header_tag
                          value_format: '%REQUEST_HEADER(tag-value)%'
                    value_fixed: 1
                    add_subtract:
                      add_log_type: DownstreamStart
                      sub_log_type: UdpPeriodic
)";

  init(config_yaml, /*autonomous_upstream=*/false,
       /*flush_access_log_on_new_request=*/true);

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},  {":authority", "envoyproxy.io"},       {":path", "/"},
      {":scheme", "http"}, {"tag-value", "my-evict-cleanup-tag"},
  };

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

  IntegrationCodecClientPtr codec_client = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response = codec_client->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  // DownstreamStart logged, gauge should be 1.
  test_server_->waitForGauge(
      "test_stat_prefix.active_requests.request_header_tag.my-evict-cleanup-tag", Eq(1));

  upstream_request_->encodeHeaders(response_headers, true);
  ASSERT_TRUE(response->waitForEndStream());

  codec_client->close();

  // Since sub_log_type is configured for UdpPeriodic (which never happens in HTTP), the explicit
  // SUB op is skipped. When the request dies, AccessLogState destructor should run and subtract the
  // gauge. The gauge should go back to 0.
  test_server_->waitForGauge(
      "test_stat_prefix.active_requests.request_header_tag.my-evict-cleanup-tag", Eq(0));
}

TEST_P(StatsAccessLogIntegrationTest, SharedScope) {
  const std::string config_yaml1 = R"EOF(
              name: envoy.access_loggers.stats
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stats_scope:
                  sharing_name: "shared_scope"
                  prefix: shared_scope_limits
                  max_counters: 1
                counters:
                  - stat:
                      name: formatcounter1
                    value_format: '%RESPONSE_CODE%'
)EOF";

  const std::string config_yaml2 = R"EOF(
              name: envoy.access_loggers.stats
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stats_scope:
                  sharing_name: "shared_scope"
                  prefix: shared_scope_limits
                  max_counters: 1
                counters:
                  - stat:
                      name: formatcounter2
                    value_format: '%RESPONSE_CODE%'
)EOF";

  init(std::vector<std::string>{config_yaml1, config_yaml2});

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":authority", "envoyproxy.io"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"}};

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  // Since both access loggers share the same configuration, they should share the same scope.
  // We expect the first counter to be incremented once (by the first access logger).
  // The second counter (from the second logger) should be dropped because the scope limit is 1.
  test_server_->waitForCounter("shared_scope_limits.formatcounter1", Eq(200));

  auto store_counter = test_server_->counter("shared_scope_limits.formatcounter2");
  EXPECT_EQ(store_counter, nullptr);
}

} // namespace

class StatsAccessLogTcpIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                         public BaseIntegrationTest {
public:
  StatsAccessLogTcpIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}
};

TEST_P(StatsAccessLogTcpIntegrationTest, ActiveTcpConnectionsGauge) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);

    const std::string tcp_proxy_config_with_access_log = R"EOF(
name: envoy.filters.network.tcp_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
  stat_prefix: tcp_stats
  cluster: cluster_0
  access_log_options:
    flush_access_log_on_start: true
  access_log:
    - name: envoy.access_loggers.stats
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
        stats_scope:
          prefix: test_stat_prefix
        gauges:
          - stat:
              name: active_connections
            value_fixed: 1
            add_subtract:
              add_log_type: TcpConnectionStart
              sub_log_type: TcpConnectionEnd
)EOF";

    TestUtility::loadFromYaml(tcp_proxy_config_with_access_log, *filter);
  });

  initialize();

  IntegrationTcpClientPtr client1 = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr raw_conn1;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(raw_conn1));
  ASSERT_TRUE(client1->connected());

  test_server_->waitForGauge("test_stat_prefix.active_connections", Eq(1));

  IntegrationTcpClientPtr client2 = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr raw_conn2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(raw_conn2));
  ASSERT_TRUE(client2->connected());

  test_server_->waitForGauge("test_stat_prefix.active_connections", Eq(2));

  client1->close();
  test_server_->waitForGauge("test_stat_prefix.active_connections", Eq(1));

  client2->close();
  test_server_->waitForGauge("test_stat_prefix.active_connections", Eq(0));
}

INSTANTIATE_TEST_SUITE_P(IpVersions, StatsAccessLogTcpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace Envoy
