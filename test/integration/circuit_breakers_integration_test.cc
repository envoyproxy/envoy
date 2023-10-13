#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "http_integration.h"
#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

class CircuitBreakersIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override { HttpProtocolIntegrationTest::initialize(); }
};

// TODO(#26236): Fix test suite for HTTP/3.
INSTANTIATE_TEST_SUITE_P(
    Protocols, CircuitBreakersIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(CircuitBreakersIntegrationTest, CircuitBreakerRetryBudgets) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);
    auto* circuit_breakers = cluster->mutable_circuit_breakers();
    auto* thresholds = circuit_breakers->add_thresholds();
    // thresholds->set_track_remaining(true);
    // thresholds->mutable_max_retries()->set_value(1);
    auto* retry_budget = thresholds->mutable_retry_budget();
    retry_budget->mutable_min_retry_concurrency()->set_value(0);
    retry_budget->mutable_budget_percent()->set_value(100);
  });
  config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
          virtual_host->clear_routes();

          auto* route_short_backoff = virtual_host->add_routes();
          route_short_backoff->mutable_match()->set_path("/route_short_backoff");
          route_short_backoff->mutable_route()->set_cluster("cluster_0");
          auto* retry_policy_short = route_short_backoff->mutable_route()->mutable_retry_policy();
          *retry_policy_short->mutable_retry_on() = "5xx";
          retry_policy_short->mutable_num_retries()->set_value(10);

          auto* route_long_backoff = virtual_host->add_routes();
          *route_long_backoff = *route_short_backoff;
          route_long_backoff->mutable_match()->set_path("/route_long_backoff");
          route_long_backoff->mutable_route()->mutable_retry_policy()->mutable_retry_back_off()->mutable_base_interval()->set_seconds(100);
        });
  initialize();

  Http::TestResponseHeaderMapImpl error_response_headers{{":status", "500"}};
  Http::TestRequestHeaderMapImpl long_backoff_rq_headers{{":method", "GET"},
                                                          {":path", "/route_long_backoff"},
                                                          {":scheme", "http"},
                                                          {":authority", "sni.lyft.com"}};
  Http::TestRequestHeaderMapImpl short_backoff_rq_headers{{":method", "GET"},
                                                          {":path", "/route_short_backoff"},
                                                          {":scheme", "http"},
                                                          {":authority", "sni.lyft.com"}};

  // EXPECT_EQ(test_server_->gauge("cluster.cluster_0.circuit_breakers.default.remaining_retries")->value(), 1);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response1 = codec_client_->makeHeaderOnlyRequest(long_backoff_rq_headers);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(error_response_headers, true); // trigger retry
  EXPECT_TRUE(upstream_request_->complete());

  test_server_->waitForCounterEq("cluster.cluster_0.upstream_rq_total", 1);
  // EXPECT_EQ(test_server_->gauge("cluster.cluster_0.circuit_breakers.default.remaining_retries")->value(), 0);
  EXPECT_EQ(test_server_->gauge("cluster.cluster_0.circuit_breakers.default.rq_retry_open")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value(), 0);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry_success")->value(), 0);

  // now we have a retry in (very long) backoff

  // make another request with short retry backoff
  Envoy::IntegrationCodecClientPtr codec_client2 = makeHttpConnection(lookupPort("http"));
  auto response2 = codec_client2->makeHeaderOnlyRequest(short_backoff_rq_headers);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(error_response_headers, true); // trigger retry
  EXPECT_TRUE(upstream_request_->complete());

  test_server_->waitForCounterEq("cluster.cluster_0.upstream_rq_total", 2);
  // EXPECT_EQ(test_server_->gauge("cluster.cluster_0.circuit_breakers.default.remaining_retries")->value(), 0);
  EXPECT_EQ(test_server_->gauge("cluster.cluster_0.circuit_breakers.default.rq_retry_open")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry_success")->value(), 0);

  // since the retry overflowed we get the error response
  ASSERT_TRUE(response2->waitForEndStream());

  // the request in long retry backoff must be manually abandoned
  // otherwise the codec destructor is annoyed about the outstanding request
  codec_client_->rawConnection().close(Envoy::Network::ConnectionCloseType::Abort);
  codec_client2->close();
}

// This test checks that triggered max requests circuit breaker
// doesn't force outlier detectors to eject an upstream host.
// See https://github.com/envoyproxy/envoy/issues/25487
TEST_P(CircuitBreakersIntegrationTest, CircuitBreakersWithOutlierDetection) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);

    // Somewhat contrived with 0, but this is the simplest way to test right now.
    auto* circuit_breakers = cluster->mutable_circuit_breakers();
    circuit_breakers->add_thresholds()->mutable_max_requests()->set_value(0);

    auto* outlier_detection = cluster->mutable_outlier_detection();
    outlier_detection->mutable_consecutive_gateway_failure()->set_value(1);
    outlier_detection->mutable_consecutive_5xx()->set_value(1);
    outlier_detection->mutable_consecutive_local_origin_failure()->set_value(1);

    outlier_detection->mutable_enforcing_consecutive_gateway_failure()->set_value(100);
    outlier_detection->mutable_enforcing_consecutive_5xx()->set_value(100);
    outlier_detection->mutable_enforcing_consecutive_local_origin_failure()->set_value(100);

    outlier_detection->set_split_external_local_origin_errors(true);

    outlier_detection->mutable_max_ejection_percent()->set_value(100);
    outlier_detection->mutable_interval()->set_nanos(1);
    outlier_detection->mutable_base_ejection_time()->set_seconds(3600);
    outlier_detection->mutable_max_ejection_time()->set_seconds(3600);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);

  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 0);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_pending_active", 0);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ("503", response->headers().getStatusValue());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_503", 1);

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_pending_overflow")->value(), 1);

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.outlier_detection.ejections_enforced_total")
                ->value(),
            0);
}

TEST_P(CircuitBreakersIntegrationTest, CircuitBreakerRuntime) {
  config_helper_.addRuntimeOverride("circuit_breakers.cluster_0.default.max_requests", "0");
  config_helper_.addRuntimeOverride("circuit_breakers.cluster_0.default.max_retries", "1024");

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);

    auto* outlier_detection = cluster->mutable_outlier_detection();
    outlier_detection->mutable_consecutive_gateway_failure()->set_value(1);
    outlier_detection->mutable_consecutive_5xx()->set_value(1);
    outlier_detection->mutable_consecutive_local_origin_failure()->set_value(1);

    outlier_detection->mutable_enforcing_consecutive_gateway_failure()->set_value(100);
    outlier_detection->mutable_enforcing_consecutive_5xx()->set_value(100);
    outlier_detection->mutable_enforcing_consecutive_local_origin_failure()->set_value(100);

    outlier_detection->set_split_external_local_origin_errors(true);

    outlier_detection->mutable_max_ejection_percent()->set_value(100);
    outlier_detection->mutable_interval()->set_nanos(1);
    outlier_detection->mutable_base_ejection_time()->set_seconds(3600);
    outlier_detection->mutable_max_ejection_time()->set_seconds(3600);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);

  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 0);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_pending_active", 0);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ("503", response->headers().getStatusValue());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_503", 1);

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_pending_overflow")->value(), 1);

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.outlier_detection.ejections_enforced_total")
                ->value(),
            0);
#ifdef ENVOY_ADMIN_FUNCTIONALITY
  auto codec_client2 = makeHttpConnection(lookupPort("admin"));
  default_request_headers_.setPath("/runtime");
  response = codec_client2->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  codec_client2->close();

  const std::string expected_json1 = R"EOF(
  "circuit_breakers.cluster_0.default.max_retries": {
)EOF";
  EXPECT_TRUE(absl::StrContains(response->body(), expected_json1)) << response->body();

  const std::string expected_json2 = R"EOF("final_value": "1024")EOF";
  EXPECT_TRUE(absl::StrContains(response->body(), expected_json2)) << response->body();
#endif
}

TEST_P(CircuitBreakersIntegrationTest, CircuitBreakerRuntimeProto) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);

    auto* outlier_detection = cluster->mutable_outlier_detection();
    outlier_detection->mutable_consecutive_gateway_failure()->set_value(1);
    outlier_detection->mutable_consecutive_5xx()->set_value(1);
    outlier_detection->mutable_consecutive_local_origin_failure()->set_value(1);

    outlier_detection->mutable_enforcing_consecutive_gateway_failure()->set_value(100);
    outlier_detection->mutable_enforcing_consecutive_5xx()->set_value(100);
    outlier_detection->mutable_enforcing_consecutive_local_origin_failure()->set_value(100);

    outlier_detection->set_split_external_local_origin_errors(true);

    outlier_detection->mutable_max_ejection_percent()->set_value(100);
    outlier_detection->mutable_interval()->set_nanos(1);
    outlier_detection->mutable_base_ejection_time()->set_seconds(3600);
    outlier_detection->mutable_max_ejection_time()->set_seconds(3600);

    auto* layer = bootstrap.mutable_layered_runtime()->add_layers();
    layer->set_name("enable layer");
    ProtobufWkt::Struct& runtime = *layer->mutable_static_layer();

    (*runtime.mutable_fields())["circuit_breakers.cluster_0.default.max_requests"].set_number_value(
        0);
    (*runtime.mutable_fields())["circuit_breakers.cluster_0.default.max_retries"].set_number_value(
        1024);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);

  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 0);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_pending_active", 0);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ("503", response->headers().getStatusValue());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_503", 1);

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_pending_overflow")->value(), 1);

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.outlier_detection.ejections_enforced_total")
                ->value(),
            0);
#ifdef ENVOY_ADMIN_FUNCTIONALITY
  auto codec_client2 = makeHttpConnection(lookupPort("admin"));
  default_request_headers_.setPath("/runtime");
  response = codec_client2->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  codec_client2->close();

  const std::string expected_json1 = R"EOF(
  "circuit_breakers.cluster_0.default.max_retries": {
)EOF";
  EXPECT_TRUE(absl::StrContains(response->body(), expected_json1)) << response->body();

  const std::string expected_json2 = R"EOF("final_value": "1024")EOF";
  EXPECT_TRUE(absl::StrContains(response->body(), expected_json2)) << response->body();
#endif
}
} // namespace
} // namespace Envoy
