#include <cstddef>

#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"
#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"

namespace Envoy {

using envoy::config::trace::v3::OpenTelemetryConfig;
using envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;
using opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest;
using opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse;

constexpr auto timeout = std::chrono::milliseconds(500);

class OpenTelemetryTraceExporterIntegrationTest
    : public testing::TestWithParam<std::tuple<int, int>>,
      public HttpIntegrationTest {
public:
  OpenTelemetryTraceExporterIntegrationTest();

  ~OpenTelemetryTraceExporterIntegrationTest() override {
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
    grpc_receiver_upstream_ = fake_upstreams_.back().get();
  }

  void setFlushIntervalMs(int64_t ms) {
    (*otel_runtime_config_.mutable_fields())["tracing.opentelemetry.flush_interval_ms"]
        .set_number_value(ms);
  }

  void setMinFlushSpans(int64_t ms) {
    (*otel_runtime_config_.mutable_fields())["tracing.opentelemetry.min_flush_spans"]
        .set_number_value(ms);
  }

  void initialize() override {
    setFlushIntervalMs(99999'000); // disable flush interval
    setUpstreamCount(1);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* grpc_receiver_cluster = bootstrap.mutable_static_resources()->add_clusters();
      grpc_receiver_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      grpc_receiver_cluster->set_name("grpc-receiver");

      auto* layer = bootstrap.mutable_layered_runtime()->add_layers();
      layer->set_name("test_otel_layer");
      auto* static_layer = layer->mutable_static_layer();
      layer->set_name("test_otel_static_layer");
      *static_layer = otel_runtime_config_;
      ConfigHelper::setHttp2(*grpc_receiver_cluster);
    });

    config_helper_.addConfigModifier([&](HttpConnectionManager& hcm) -> void {
      HttpConnectionManager::Tracing tracing;
      tracing.mutable_random_sampling()->set_value(100);
      tracing.mutable_spawn_upstream_span()->set_value(true);

      OpenTelemetryConfig otel_config;
      otel_config.set_service_name("my-service");
      otel_config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("grpc-receiver");
      *otel_config.mutable_grpc_service()->mutable_timeout() =
          Protobuf::util::TimeUtil::MillisecondsToDuration(250);

      tracing.mutable_provider()->set_name("envoy.tracers.opentelemetry");
      tracing.mutable_provider()->mutable_typed_config()->PackFrom(otel_config);

      *hcm.mutable_tracing() = tracing;
    });
    HttpIntegrationTest::initialize();
  }

  void cleanup() { cleanupUpstreamAndDownstream(); }

  void doHttpRequest() {
    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

    auto response = sendRequestAndWaitForResponse(default_request_headers_, 0,
                                                  default_response_headers_, 0, 0, timeout);

    codec_client_->close();
    auto _ = codec_client_->waitForDisconnect(timeout);
  }

  FakeUpstream* grpc_receiver_upstream_{};
  ProtobufWkt::Struct otel_runtime_config_;

  FakeHttpConnectionPtr connection_;
  std::vector<FakeStreamPtr> streams_;
};

struct TestCase {};

OpenTelemetryTraceExporterIntegrationTest::OpenTelemetryTraceExporterIntegrationTest()
    : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4){};

INSTANTIATE_TEST_SUITE_P(All, OpenTelemetryTraceExporterIntegrationTest,
                         // values are (min_flush_spans, num_requests)
                         testing::Values(std::make_tuple(1, 1), std::make_tuple(1, 2),
                                         std::make_tuple(2, 1), std::make_tuple(2, 2),
                                         std::make_tuple(5, 5), std::make_tuple(6, 3)));

TEST_P(OpenTelemetryTraceExporterIntegrationTest, GrpcExporter) {
  auto [min_flush_spans, num_requests] = GetParam();
  setMinFlushSpans(min_flush_spans);

  initialize();

  dispatcher_->post([this, num_requests = num_requests]() {
    // each request will create two spans, one upstream and one downstream
    for (auto i = 0; i < num_requests; i++) {
      doHttpRequest();
    }
  });

  // verify that we receive the correct number of export requests, each with the correct number
  // of spans (there should be no unexported spans remaining)
  auto num_expected_exports = (num_requests * 2) / min_flush_spans;
  ASSERT_TRUE(grpc_receiver_upstream_->waitForHttpConnection(*dispatcher_, connection_));

  std::map<std::string, int> name_counts;
  for (auto i = 0; i < num_expected_exports; i++) {
    FakeStreamPtr stream;
    ASSERT_TRUE(connection_->waitForNewStream(*dispatcher_, stream, timeout))
        << "Expected to receive " << num_expected_exports << " export requests, but got " << i;
    ExportTraceServiceRequest req;
    ASSERT_TRUE(stream->waitForGrpcMessage(*dispatcher_, req, timeout));
    stream->startGrpcStream();
    ExportTraceServiceResponse resp;
    stream->sendGrpcMessage(resp);
    stream->finishGrpcStream(Grpc::Status::WellKnownGrpcStatus::Ok);

    ASSERT_EQ(1, req.resource_spans().size());
    ASSERT_EQ(1, req.resource_spans(0).scope_spans().size());
    ASSERT_EQ(min_flush_spans, req.resource_spans(0).scope_spans(0).spans().size());
    for (auto j = 0; j < min_flush_spans; j++) {
      ++name_counts[req.resource_spans(0).scope_spans(0).spans().at(j).name()];
    }
    ASSERT_TRUE(stream->waitForEndStream(*dispatcher_, timeout));
    streams_.push_back(std::move(stream));
  }

  // the number of upstream and downstream spans received should be equal
  ASSERT_EQ(2, name_counts.size());
  ASSERT_THAT(name_counts,
              testing::AllOf(testing::Contains(testing::Pair("ingress", testing::Eq(num_requests))),
                             testing::Contains(testing::Pair("router cluster_0 egress",
                                                             testing::Eq(num_requests)))));

  cleanup();
}

} // namespace Envoy
