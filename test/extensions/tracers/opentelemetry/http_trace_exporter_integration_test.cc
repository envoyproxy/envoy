#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

using envoy::config::trace::v3::OpenTelemetryConfig;
using envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;

// Integration test that verifies request_headers_to_add with a substitution formatter
// is correctly applied when the OpenTelemetry tracer uses an HTTP exporter.
class OpenTelemetryHttpTraceExporterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  OpenTelemetryHttpTraceExporterIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  ~OpenTelemetryHttpTraceExporterIntegrationTest() override {
    if (connection_) {
      AssertionResult result = connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      connection_.reset();
    }
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
    trace_receiver_upstream_ = fake_upstreams_.back().get();
  }

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* trace_cluster = bootstrap.mutable_static_resources()->add_clusters();
      trace_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      trace_cluster->set_name("trace-receiver");
      ConfigHelper::setHttp2(*trace_cluster);

      // Set a short flush interval so spans are exported quickly.
      auto* layer = bootstrap.mutable_layered_runtime()->add_layers();
      layer->set_name("test_otel_static_layer");
      Protobuf::Struct runtime_config;
      (*runtime_config.mutable_fields())["tracing.opentelemetry.flush_interval_ms"]
          .set_number_value(5);
      (*runtime_config.mutable_fields())["tracing.opentelemetry.min_flush_spans"].set_number_value(
          1);
      *layer->mutable_static_layer() = runtime_config;
    });

    config_helper_.addConfigModifier([this](HttpConnectionManager& hcm) -> void {
      HttpConnectionManager::Tracing tracing;
      tracing.mutable_random_sampling()->set_value(100);
      tracing.mutable_spawn_upstream_span()->set_value(false);

      OpenTelemetryConfig otel_config;
      otel_config.set_service_name("my-service");

      auto* http_service = otel_config.mutable_http_service();
      auto* http_uri = http_service->mutable_http_uri();
      http_uri->set_uri(fmt::format("http://{}:{}/v1/traces",
                                    Network::Test::getLoopbackAddressUrlString(GetParam()),
                                    trace_receiver_upstream_->localAddress()->ip()->port()));
      http_uri->set_cluster("trace-receiver");
      http_uri->mutable_timeout()->set_seconds(1);

      auto* header = http_service->add_request_headers_to_add();
      header->mutable_header()->set_key("x-custom-formatter");
      header->mutable_header()->set_value("%HOSTNAME%");

      tracing.mutable_provider()->set_name("envoy.tracers.opentelemetry");
      tracing.mutable_provider()->mutable_typed_config()->PackFrom(otel_config);

      *hcm.mutable_tracing() = tracing;
    });

    HttpIntegrationTest::initialize();
  }

  FakeUpstream* trace_receiver_upstream_{};
  FakeHttpConnectionPtr connection_;
  FakeStreamPtr stream_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, OpenTelemetryHttpTraceExporterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(OpenTelemetryHttpTraceExporterIntegrationTest, HttpExportWithFormatterHeader) {
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  codec_client_->close();

  // Wait for the trace export HTTP request.
  ASSERT_TRUE(trace_receiver_upstream_->waitForHttpConnection(*dispatcher_, connection_));
  ASSERT_TRUE(connection_->waitForNewStream(*dispatcher_, stream_));
  ASSERT_TRUE(stream_->waitForEndStream(*dispatcher_));

  // Verify standard OTLP HTTP headers.
  EXPECT_EQ("POST", stream_->headers().getMethodValue());
  EXPECT_EQ("/v1/traces", stream_->headers().getPathValue());

  // Verify the custom formatter header was applied.
  auto values = stream_->headers().get(Http::LowerCaseString("x-custom-formatter"));
  ASSERT_FALSE(values.empty());
  EXPECT_FALSE(values[0]->value().empty());
  EXPECT_NE(values[0]->value(), "%HOSTNAME%");

  stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
}

} // namespace Envoy
