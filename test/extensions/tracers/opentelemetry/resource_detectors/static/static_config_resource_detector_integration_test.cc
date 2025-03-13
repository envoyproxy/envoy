#include <memory>
#include <string>

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/extensions/tracers/opentelemetry/http_trace_exporter.h"

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

class StaticConfigResourceDetectorIntegrationTest
    : public Envoy::HttpIntegrationTest,
      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  StaticConfigResourceDetectorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initializeWithYaml(const std::string& filter_config) {
    // Create static clusters.
    createClusters();

    auto tracing_config =
        std::make_unique<::envoy::extensions::filters::network::http_connection_manager::v3::
                             HttpConnectionManager_Tracing>();
    TestUtility::loadFromYaml(filter_config, *tracing_config.get());
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void { hcm.set_allocated_tracing(tracing_config.release()); });

    initialize();
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void createClusters() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* opentelemetry_cluster = bootstrap.mutable_static_resources()->add_clusters();
      opentelemetry_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      opentelemetry_cluster->set_name("opentelemetry_collector");
      ConfigHelper::setHttp2(*opentelemetry_cluster);
    });
  }

  void cleanup() {
    codec_client_->close();
    if (fake_backend_connection_ != nullptr) {
      AssertionResult result = fake_backend_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_backend_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
  }

  FakeHttpConnectionPtr fake_backend_connection_;
  FakeStreamPtr backend_request_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, StaticConfigResourceDetectorIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify Custom attributes in resource object are inserted
TEST_P(StaticConfigResourceDetectorIntegrationTest, TestResourceAttributeSet) {

  const std::string yaml_string = R"EOF(
  provider:
    name: envoy.tracers.opentelemetry
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
      http_service:
        http_uri:
          uri: https://stg.ingest.mulesoft.com/api/v2/traces
          cluster: opentelemetry_collector
          timeout: "10s"
      service_name: "a_service_name"
      resource_detectors:
        - name: envoy.tracers.opentelemetry.resource_detectors.static_config
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.tracers.opentelemetry.resource_detectors.v3.StaticConfigResourceDetectorConfig
            attributes:
              key1: value1
              key2: value2
  )EOF";

  initializeWithYaml(yaml_string);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_backend_connection_));
  ASSERT_TRUE(fake_backend_connection_->waitForNewStream(*dispatcher_, backend_request_));
  ASSERT_TRUE(backend_request_->waitForEndStream(*dispatcher_));

  // Sanity checking that we sent the expected data.
  EXPECT_THAT(backend_request_->headers(), HeaderValueOf(Http::Headers::get().Method, "POST"));
  EXPECT_THAT(backend_request_->headers(),
              HeaderValueOf(Http::Headers::get().Path, "/api/v2/traces"));

  backend_request_->encodeHeaders(default_response_headers_, true /*end_stream*/);

  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest message;
  Buffer::InstancePtr message_buffer = std::make_unique<Buffer::OwnedImpl>();
  message_buffer->add(backend_request_->body());
  Buffer::ZeroCopyInputStreamImpl request_stream(std::move(message_buffer));
  EXPECT_TRUE(message.ParseFromZeroCopyStream(&request_stream));

  // Sanity checking that expected attributes are all present.
  absl::flat_hash_map<std::string, std::string> expected_attributes = {{"key1", "value1"},
                                                                       {"key2", "value2"}};
  int matched_attributes_count = 0;
  ASSERT_TRUE(message.resource_spans().size() > 0);
  for (auto& actual : message.resource_spans()[0].resource().attributes()) {
    auto expected = expected_attributes.find(actual.key());
    if (expected != expected_attributes.end()) {
      EXPECT_EQ(expected->second, actual.value().string_value());
      ++matched_attributes_count;
    }
  }
  EXPECT_EQ(matched_attributes_count, expected_attributes.size());

  cleanup();
}

} // namespace
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
