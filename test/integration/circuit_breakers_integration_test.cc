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
    Protobuf::Struct& runtime = *layer->mutable_static_layer();

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

class OutlierDetectionIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override { HttpProtocolIntegrationTest::initialize(); }
};

INSTANTIATE_TEST_SUITE_P(
    Protocols, OutlierDetectionIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),

    HttpProtocolIntegrationTest::protocolTestParamsToString);

// Test verifies that empty outlier detection setting in protocol options
// do not interfere with existing outlier detection.
TEST_P(OutlierDetectionIntegrationTest, NoClusterOverwrite) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);

    cluster->mutable_common_lb_config()->mutable_healthy_panic_threshold()->set_value(0);

    auto* outlier_detection = cluster->mutable_outlier_detection();

    TestUtility::loadFromYaml(R"EOF(
      consecutive_5xx: 2
      max_ejection_percent: 100
        )EOF",
                              *outlier_detection);

    ConfigHelper::HttpProtocolOptions protocol_options;
    std::string protocol_options_yaml;
    if (absl::StrContains(::testing::UnitTest::GetInstance()->current_test_info()->name(),
                          "Http2Upstream")) {
      protocol_options_yaml += R"EOF(
         explicit_http_config:
           http2_protocol_options: {}
    )EOF";
    } else {
      ASSERT(absl::StrContains(::testing::UnitTest::GetInstance()->current_test_info()->name(),
                               "HttpUpstream"));
      protocol_options_yaml += R"EOF(
         explicit_http_config:
           http_protocol_options: {}
    )EOF";
    }
    TestUtility::loadFromYaml(protocol_options_yaml, protocol_options);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  // return en error from upstream server.
  default_response_headers_.setStatus(500);
  for (auto i = 1; i <= 2; i++) {
    auto response =
        sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

    ASSERT_TRUE(response->waitForEndStream());
    // 500 error should be propagated to downstream client.
    EXPECT_EQ("500", response->headers().getStatusValue());
  }

  // Send another request. It should not reach upstream and should be handled by envoy.
  // The only existing endpoint in the cluster has been marked as unhealthy.
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());

  codec_client_->close();
}

// Test verifies that non-5xx codes defined in cluster's protocol options
// are thread as errors and cause outlier detection to mark a host as unhealthy.
TEST_P(OutlierDetectionIntegrationTest, ClusterOverwriteNon5xxAsErrors) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);

    cluster->mutable_common_lb_config()->mutable_healthy_panic_threshold()->set_value(0);

    auto* outlier_detection = cluster->mutable_outlier_detection();

    TestUtility::loadFromYaml(R"EOF(
      consecutive_5xx: 2
      max_ejection_percent: 100
        )EOF",
                              *outlier_detection);

    ConfigHelper::HttpProtocolOptions protocol_options;
    std::string protocol_options_yaml;
    if (absl::StrContains(::testing::UnitTest::GetInstance()->current_test_info()->name(),
                          "Http2Upstream")) {
      protocol_options_yaml += R"EOF(
         explicit_http_config:
           http2_protocol_options: {}
    )EOF";
    } else {
      ASSERT(absl::StrContains(::testing::UnitTest::GetInstance()->current_test_info()->name(),
                               "HttpUpstream"));
      protocol_options_yaml += R"EOF(
         explicit_http_config:
           http_protocol_options: {}
    )EOF";
    }

    // Configure any response with code 300-305 or response test-header containing
    // string "treat-as-error" to be treated as 5xx code.
    protocol_options_yaml += R"EOF(
         outlier_detection:
           error_matcher:
             or_match:
               rules:
               - http_response_headers_match:
                   headers:
                     - name: ":status"
                       range_match:
                         start: 300
                         end: 305
               - http_response_headers_match:
                   headers:
                     - name: "test-header"
                       string_match:
                         contains: "treat-as-error"
    )EOF",

        TestUtility::loadFromYaml(protocol_options_yaml, protocol_options);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // respond with status code 301. It should be treated as error.
  default_response_headers_.setStatus(301);
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("301", response->headers().getStatusValue());

  // Respond with status code 200 with "test-header".
  // It should be treated as error.
  default_response_headers_.setStatus(200);
  default_response_headers_.appendCopy(Http::LowerCaseString("test-header"), "treat-as-error");

  response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Now send a request. It will be captured by Envoy and 503 will be returned as the only upstream
  // is unhealthy now..
  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());

  codec_client_->close();
}
// Test verifies that 5xx gateway errors configured in cluster protocol options are
// forwarded to outlier detection in the original form and are not converted to code 500.
TEST_P(OutlierDetectionIntegrationTest, ClusterOverwriteGatewayErrors) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);

    cluster->mutable_common_lb_config()->mutable_healthy_panic_threshold()->set_value(0);

    auto* outlier_detection = cluster->mutable_outlier_detection();

    TestUtility::loadFromYaml(R"EOF(
      consecutive_5xx: 0
      consecutive_gateway_failure: 2
      enforcing_consecutive_gateway_failure: 100
      max_ejection_percent: 100
        )EOF",
                              *outlier_detection);

    ConfigHelper::HttpProtocolOptions protocol_options;
    std::string protocol_options_yaml;
    if (absl::StrContains(::testing::UnitTest::GetInstance()->current_test_info()->name(),
                          "Http2Upstream")) {
      protocol_options_yaml += R"EOF(
         explicit_http_config:
           http2_protocol_options: {}
    )EOF";
    } else {
      ASSERT(absl::StrContains(::testing::UnitTest::GetInstance()->current_test_info()->name(),
                               "HttpUpstream"));
      protocol_options_yaml += R"EOF(
         explicit_http_config:
           http_protocol_options: {}
    )EOF";
    }

    protocol_options_yaml += R"EOF(
         outlier_detection:
           error_matcher:
               http_response_headers_match:
                 headers:
                   - name: ":status"
                     range_match:
                       start: 502
                       end: 503
    )EOF",

        TestUtility::loadFromYaml(protocol_options_yaml, protocol_options);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_response_headers_.setStatus(502);
  for (auto i = 1; i <= 2; i++) {
    auto response =
        sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("502", response->headers().getStatusValue());
  }

  // Now send a request. It will be captured by Envoy and 503 will be returned as the only upstream
  // is unhealthy now..
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());

  codec_client_->close();
}

} // namespace
} // namespace Envoy
