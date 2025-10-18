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

// Parameterized integration test. The parameters in the tupple are:
// - name of the test
// - basic (legacy) outlier detection config snippet
// - outlier detection extension config snippet to be added to the cluster
// - response code to be sent from upstream 
class OutlierDetectionIntegrationTest : public HttpProtocolIntegrationTestWithParams<std::tuple<std::string, absl::string_view, absl::string_view, uint32_t, absl::string_view>> {
public:
  static constexpr size_t TEST_NAME = 0;
  static constexpr size_t BASIC_OD_CONFIG = 1;
  static constexpr size_t EXT_OD_CONFIG = 2;
  static constexpr size_t RESPONSE_CODE = 3;
  static constexpr size_t OPTIONAL_HEADER = 4;

    // TODO: can I define a type in the template and here just use that type instead of repeating whole thing?
    static std::string protocolTestParamsToString(
const ::testing::TestParamInfo<std::tuple<HttpProtocolTestParams, std::tuple<std::string, absl::string_view, absl::string_view, uint32_t, absl::string_view>>>& params) {
        
        return absl::StrCat(HttpProtocolIntegrationTestBase::testNameFromTestParams(std::get<0>(params.param)), "_",
               std::get<TEST_NAME>(std::get<1>(params.param)));
    }
    // Set of basic outlier detection configs. 
    static constexpr absl::string_view consecutive_5xx_only = R"EOF(
      consecutive_5xx: 2
      max_ejection_percent: 100
    )EOF";

    static constexpr absl::string_view gateway_only = R"EOF(
      consecutive_5xx: 0
      consecutive_gateway_failure: 2
      enforcing_consecutive_gateway_failure: 100
      max_ejection_percent: 100
        )EOF";


    // set of different outlier detection extension configs.
    // Configure any response with code 300-305 or response test-header containing
    // string "treat-as-error" to be treated as 5xx code.
    static constexpr absl::string_view error_3xx_and_header = R"EOF(
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
    )EOF";

    static constexpr absl::string_view gateway_error = R"EOF(
         outlier_detection:
           error_matcher:
               http_response_headers_match:
                 headers:
                   - name: ":status"
                     range_match:
                       start: 502
                       end: 503
    )EOF";
};

// Test configures outlier detection extensions in cluster.
// Then it sends several http requests and responses.
// Responses match configured outlier extensions and cause the endpoint to be marked
// as unhealthy.
TEST_P(OutlierDetectionIntegrationTest, ExtensionsTest) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);

    cluster->mutable_common_lb_config()->mutable_healthy_panic_threshold()->set_value(0);

    auto* outlier_detection = cluster->mutable_outlier_detection();

    TestUtility::loadFromYaml(std::string(std::get<BASIC_OD_CONFIG>(std::get<1>(GetParam()))),
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

    // Add config outlier detection extensionconfig
    protocol_options_yaml += std::get<EXT_OD_CONFIG>(std::get<1>(GetParam()));

        TestUtility::loadFromYaml(protocol_options_yaml, protocol_options);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Set the response code.
  const uint32_t response_code = std::get<RESPONSE_CODE>(std::get<1>(GetParam()));
  default_response_headers_.setStatus(response_code);
  // Add header if test parameter includes one.
  if (!std::get<OPTIONAL_HEADER>(std::get<1>(GetParam())).empty()) {
  default_response_headers_.appendCopy(Http::LowerCaseString("test-header"), std::get<OPTIONAL_HEADER>(std::get<1>(GetParam())));
    }
  for (auto i = 1; i <= 2; i++) {
    auto response =
        sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ(std::to_string(response_code), response->headers().getStatusValue());
  }

  // Now send a request. It will be captured by Envoy and 503 will be returned as the only upstream
  // is unhealthy now.
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());

  codec_client_->close();
}

INSTANTIATE_TEST_SUITE_P(
    Protocols, OutlierDetectionIntegrationTest,
    testing::Combine(testing::ValuesIn(HttpProtocolIntegrationTestBase::getProtocolTestParamsWithoutHTTP3()), 
                     testing::Values(
                        // Regression test.  Test verifies that empty outlier detection setting in protocol options
                        // do not interfere with existing outlier detection.
                        std::make_tuple(std::string("test1"), OutlierDetectionIntegrationTest::consecutive_5xx_only, std::string(""), 500, ""),
                        // In this test, outlier extentions define 3xx codes as errors.
                        std::make_tuple(std::string("test2"), OutlierDetectionIntegrationTest::consecutive_5xx_only, 
                        OutlierDetectionIntegrationTest::error_3xx_and_header, 301, ""),
                        // Test verifies that when outlier extensions include gateway code 502, that code
                        // is forwarded in original form and is not converted to generic code 500.
                        std::make_tuple(std::string("test3"), OutlierDetectionIntegrationTest::gateway_only, 
                        OutlierDetectionIntegrationTest::gateway_error, 502, ""),
                        // Test verifies that responses where a matcher matches not code, but response
                        // header are treated as errors.
                        std::make_tuple(std::string("test4"), OutlierDetectionIntegrationTest::consecutive_5xx_only, 
                        OutlierDetectionIntegrationTest::error_3xx_and_header, 200, "treat-as-error")
                        )),
    OutlierDetectionIntegrationTest::protocolTestParamsToString);

} // namespace
} // namespace Envoy
