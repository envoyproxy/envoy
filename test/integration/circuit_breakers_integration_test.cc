#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

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
