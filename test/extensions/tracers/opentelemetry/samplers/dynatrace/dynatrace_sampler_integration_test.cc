#include <memory>
#include <string>

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {
namespace {

const char* TRACEPARENT_VALUE = "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01";
const char* TRACEPARENT_VALUE_START = "00-0af7651916cd43dd8448eb211c80319c";

class DynatraceSamplerIntegrationTest : public Envoy::HttpIntegrationTest,
                                        public testing::TestWithParam<Network::Address::IpVersion> {
public:
  DynatraceSamplerIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    const std::string yaml_string = R"EOF(
  provider:
    name: envoy.tracers.opentelemetry
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
      grpc_service:
        envoy_grpc:
          cluster_name: opentelemetry_collector
        timeout: 0.250s
      service_name: "a_service_name"
      sampler:
        name: envoy.tracers.opentelemetry.samplers.dynatrace
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.tracers.opentelemetry.samplers.v3.DynatraceSamplerConfig
          tenant: "abc12345"
          cluster_id: -1743916452
  )EOF";

    auto tracing_config =
        std::make_unique<::envoy::extensions::filters::network::http_connection_manager::v3::
                             HttpConnectionManager_Tracing>();
    TestUtility::loadFromYaml(yaml_string, *tracing_config.get());
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void { hcm.set_allocated_tracing(tracing_config.release()); });

    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynatraceSamplerIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Sends a request with traceparent and tracestate header.
TEST_P(DynatraceSamplerIntegrationTest, TestWithTraceparentAndTracestate) {
  // tracestate does not contain a Dynatrace tag
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},     {":path", "/test/long/url"}, {":scheme", "http"},
      {":authority", "host"}, {"tracestate", "key=value"}, {"traceparent", TRACEPARENT_VALUE}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  // traceparent should be set: traceid should be re-used, span id should be different
  absl::string_view traceparent_value = upstream_request_->headers()
                                            .get(Http::LowerCaseString("traceparent"))[0]
                                            ->value()
                                            .getStringView();
  EXPECT_TRUE(absl::StartsWith(traceparent_value, TRACEPARENT_VALUE_START));
  EXPECT_NE(TRACEPARENT_VALUE, traceparent_value);
  // Dynatrace tracestate should be added to existing tracestate
  absl::string_view tracestate_value = upstream_request_->headers()
                                           .get(Http::LowerCaseString("tracestate"))[0]
                                           ->value()
                                           .getStringView();
  // use StartsWith because path-info (last element in trace state) contains a random value
  EXPECT_TRUE(absl::StartsWith(tracestate_value, "5b3f9fed-980df25c@dt=fw4;0;0;0;0;0;0;"))
      << "Received tracestate: " << tracestate_value;
  EXPECT_TRUE(absl::StrContains(tracestate_value, ",key=value"))
      << "Received tracestate: " << tracestate_value;
}

// Sends a request with traceparent but no tracestate header.
TEST_P(DynatraceSamplerIntegrationTest, TestWithTraceparentOnly) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"traceparent", TRACEPARENT_VALUE}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  // traceparent should be set: traceid should be re-used, span id should be different
  absl::string_view traceparent_value = upstream_request_->headers()
                                            .get(Http::LowerCaseString("traceparent"))[0]
                                            ->value()
                                            .getStringView();
  EXPECT_TRUE(absl::StartsWith(traceparent_value, TRACEPARENT_VALUE_START));
  EXPECT_NE(TRACEPARENT_VALUE, traceparent_value);
  // Dynatrace tag should be added to tracestate
  absl::string_view tracestate_value = upstream_request_->headers()
                                           .get(Http::LowerCaseString("tracestate"))[0]
                                           ->value()
                                           .getStringView();
  // use StartsWith because path-info (last element in trace state contains a random value)
  EXPECT_TRUE(absl::StartsWith(tracestate_value, "5b3f9fed-980df25c@dt=fw4;0;0;0;0;0;0;"))
      << "Received tracestate: " << tracestate_value;
}

// Sends a request without traceparent and tracestate header.
TEST_P(DynatraceSamplerIntegrationTest, TestWithoutTraceparentAndTracestate) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  // traceparent will be added, trace_id and span_id will be generated, so there is nothing we can
  // assert
  EXPECT_EQ(upstream_request_->headers().get(::Envoy::Http::LowerCaseString("traceparent")).size(),
            1);
  // Dynatrace tag should be added to tracestate
  absl::string_view tracestate_value = upstream_request_->headers()
                                           .get(Http::LowerCaseString("tracestate"))[0]
                                           ->value()
                                           .getStringView();
  EXPECT_TRUE(absl::StartsWith(tracestate_value, "5b3f9fed-980df25c@dt=fw4;0;0;0;0;0;0;"))
      << "Received tracestate: " << tracestate_value;
}

} // namespace
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
