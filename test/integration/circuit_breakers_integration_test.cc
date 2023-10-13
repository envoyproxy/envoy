#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "test/integration/http_protocol_integration.h"

#include "http_integration.h"

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

class RetryBudgetIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override { HttpProtocolIntegrationTest::initialize(); }
};

INSTANTIATE_TEST_SUITE_P(Protocols, RetryBudgetIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(RetryBudgetIntegrationTest, CircuitBreakerRetryBudgets) {
  // Create a config with a retry budget of 100% and a (very) long retry backoff
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* retry_budget = bootstrap.mutable_static_resources()
                             ->mutable_clusters(0)
                             ->mutable_circuit_breakers()
                             ->add_thresholds()
                             ->mutable_retry_budget();
    retry_budget->mutable_min_retry_concurrency()->set_value(0);
    retry_budget->mutable_budget_percent()->set_value(100);
  });
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* retry_policy =
            hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_retry_policy();
        retry_policy->set_retry_on("5xx");
        retry_policy->mutable_retry_back_off()->mutable_base_interval()->set_seconds(100);
      });
  initialize();

  Http::TestResponseHeaderMapImpl error_response_headers{{":status", "500"}};

  // Send a request that will fail and trigger a retry
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response1 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(error_response_headers, true); // trigger retry
  EXPECT_TRUE(upstream_request_->complete());

  // Observe odd behavior of retry overflow even though the budget is set to 100%
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_rq_total", 1);
  if (upstreamProtocol() == Http::CodecType::HTTP2) {
    // For H2 upstreams the observed behavior is that the retry budget max is 0
    // i.e. it doesn't count the request that just failed as active
    EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value(), 1);
    // since the retry overflowed we get the error response
    ASSERT_TRUE(response1->waitForEndStream());
  } else {
    // For H1/H3 upstreams the observed behavior is that the retry budget max is 1
    // i.e. it counts the request that just failed as still active when calculating retries
    //
    // Now this retry is in backoff and not counted as an active or pending request,
    // but still counted against the retry limit
    EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value(), 0);
  }

  // now we are in a slightly weird protocol-dependent state:
  // - For H2: the request is completed and there is no retry happening
  // - For H1/H3: the request is in a long retry backoff

  // make another request that will fail and trigger another retry
  Envoy::IntegrationCodecClientPtr codec_client2 = makeHttpConnection(lookupPort("http"));
  auto response2 = codec_client2->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(error_response_headers, true); // trigger retry
  EXPECT_TRUE(upstream_request_->complete());

  test_server_->waitForCounterEq("cluster.cluster_0.upstream_rq_total", 2);
  // This time behavior is independent of upstream protocol, no matter what the retry overflows
  //
  // In the case of H2, it would overflow regardless of the retry in backoff due to the behavior
  // seen above
  //
  // However, for H1/H3 the retry in backoff is counted against the active retry count, but not
  // counted against the limit (active + pending requests) so we have:
  // - 1 retry in backoff
  // - 1 request counted as active (due to H1/H3 specific behavior seen above)
  // Because our retry budget is 100%, that gives us a retry limit of 1 since the retry in backoff
  // is not counted in active + pending requests So we overflow the retry budget on the second
  // request
  if (upstreamProtocol() == Http::CodecType::HTTP2) {
    EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value(), 2);
  } else {
    EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_retry_overflow")->value(), 1);
  }

  // since the retry overflowed we get the error response
  ASSERT_TRUE(response2->waitForEndStream());

  if (upstreamProtocol() != Http::CodecType::HTTP2) {
    // For H1/H3: the first request is in a long retry backoff and must be manually abandoned
    // otherwise the codec destructor is annoyed about the outstanding request
    codec_client_->rawConnection().close(Envoy::Network::ConnectionCloseType::Abort);
  }
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
