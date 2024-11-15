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

class EnvironmentResourceDetectorIntegrationTest
    : public Envoy::HttpIntegrationTest,
      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  EnvironmentResourceDetectorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {

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
      resource_detectors:
        - name: envoy.tracers.opentelemetry.resource_detectors.environment
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.tracers.opentelemetry.resource_detectors.v3.EnvironmentResourceDetectorConfig
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

INSTANTIATE_TEST_SUITE_P(IpVersions, EnvironmentResourceDetectorIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify Envoy starts even when the Environment resource detector wasn't able to detect any
// attributes (env variable OTEL_RESOURCE_ATTRIBUTES not set)
TEST_P(EnvironmentResourceDetectorIntegrationTest, TestWithNoEnvVariableSet) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");
}

} // namespace
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
