#include "source/extensions/tracers/opentelemetry/resource_detectors/per_route/resource_typed_metadata.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "api/envoy/config/trace/v3/opentelemetry.pb.h"
#include "api/envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "api/envoy/extensions/tracers/opentelemetry/resource_detectors/v3/per_route_resource_metadata.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "third_party/opentelemetry/proto/collector/trace/v1/trace_service.pb.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {
namespace {

using envoy::config::trace::v3::OpenTelemetryConfig;
using envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;
using envoy::extensions::tracers::opentelemetry::resource_detectors::v3::PerRouteResourceMetadata;
using opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest;
using opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse;

constexpr auto timeout = std::chrono::milliseconds(500);

class PerRouteResourceDetectorIntegrationTest : public HttpIntegrationTest, public testing::Test {
public:
  PerRouteResourceDetectorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
    grpc_receiver_upstream_ = fake_upstreams_.back().get();
  }

  void setMinFlushSpans(int64_t ms) {
    (*otel_runtime_config_.mutable_fields())["tracing.opentelemetry.min_flush_spans"]
        .set_number_value(ms);
  }

  void initialize() override {
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
      ConfigHelper::setHttp2(*grpc_receiver_cluster);
    });

    config_helper_.addConfigModifier([&](HttpConnectionManager& hcm) -> void {
      HttpConnectionManager::Tracing tracing;
      tracing.mutable_random_sampling()->set_value(100);
      tracing.mutable_spawn_upstream_span()->set_value(false);

      OpenTelemetryConfig otel_config;
      otel_config.set_service_name("my-service");
      otel_config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("grpc-receiver");
      *otel_config.mutable_grpc_service()->mutable_timeout() =
          Protobuf::util::TimeUtil::MillisecondsToDuration(250);

      auto* resource_detector = otel_config.add_resource_detectors();
      resource_detector->set_name("envoy.tracers.opentelemetry.resource_detectors.per_route");
      resource_detector->mutable_typed_config()->PackFrom(
          envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
              PerRouteResourceMetadata());

      tracing.mutable_provider()->set_name("envoy.tracers.opentelemetry");
      tracing.mutable_provider()->mutable_typed_config()->PackFrom(otel_config);

      *hcm.mutable_tracing() = tracing;
    });

    HttpIntegrationTest::initialize();
  }

  void cleanup() { cleanupUpstreamAndDownstream(); }

  FakeUpstream* grpc_receiver_upstream_{};
  FakeHttpConnectionPtr connection_;
  Protobuf::Struct otel_runtime_config_;
};

TEST_F(PerRouteResourceDetectorIntegrationTest, PerRouteResource) {
  PerRouteResourceMetadata metadata;
  metadata.mutable_attributes()->insert({"key", "value"});
  ProtobufWkt::Any packed_metadata;
  packed_metadata.PackFrom(metadata);

  config_helper_.addConfigModifier([&](HttpConnectionManager& hcm) -> void {
    auto* route_config = hcm.mutable_route_config();
    auto* virtual_host = route_config->mutable_virtual_hosts(0);
    auto* route = virtual_host->mutable_routes(0);
    (*route->mutable_metadata()->mutable_typed_filter_metadata())
        ["envoy.tracers.opentelemetry.resource_typed_metadata"] = packed_metadata;
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  codec_client_->close();
  auto _ = codec_client_->waitForDisconnect();

  ASSERT_TRUE(grpc_receiver_upstream_->waitForHttpConnection(*dispatcher_, connection_));
  FakeStreamPtr stream;
  ASSERT_TRUE(connection_->waitForNewStream(*dispatcher_, stream, timeout));
  ExportTraceServiceRequest req;
  ASSERT_TRUE(stream->waitForGrpcMessage(*dispatcher_, req, timeout));
  stream->startGrpcStream();
  ExportTraceServiceResponse resp;
  stream->sendGrpcMessage(resp);
  stream->finishGrpcStream(Grpc::Status::WellKnownGrpcStatus::Ok);
  ASSERT_TRUE(stream->waitForEndStream(*dispatcher_, timeout));
  EXPECT_EQ(1, req.resource_spans().size());
  // Per-route resource attributes are added to the resource spans
  bool found = false;
  for (const auto& attribute : req.resource_spans(0).resource().attributes()) {
    if (attribute.key() == "key" && attribute.value().string_value() == "value") {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);

  cleanup();

  ASSERT_TRUE(connection_->close());
  ASSERT_TRUE(connection_->waitForDisconnect());
  connection_.reset();
}

TEST_F(PerRouteResourceDetectorIntegrationTest, MultipleRoutes) {
  setMinFlushSpans(1);

  PerRouteResourceMetadata metadata_1;
  metadata_1.mutable_attributes()->insert({"route", "1"});
  ProtobufWkt::Any packed_metadata_1;
  packed_metadata_1.PackFrom(metadata_1);

  PerRouteResourceMetadata metadata_2;
  metadata_2.mutable_attributes()->insert({"route", "2"});
  ProtobufWkt::Any packed_metadata_2;
  packed_metadata_2.PackFrom(metadata_2);

  config_helper_.addConfigModifier([&](HttpConnectionManager& hcm) -> void {
    auto* route_config = hcm.mutable_route_config();
    auto* virtual_host = route_config->mutable_virtual_hosts(0);

    auto* route_1 = virtual_host->mutable_routes(0);
    route_1->mutable_match()->set_path("/route1");
    (*route_1->mutable_metadata()->mutable_typed_filter_metadata())
        ["envoy.tracers.opentelemetry.resource_typed_metadata"] = packed_metadata_1;

    auto* route_2 = virtual_host->add_routes();
    route_2->mutable_match()->set_path("/route2");
    route_2->mutable_route()->set_cluster("cluster_0");
    (*route_2->mutable_metadata()->mutable_typed_filter_metadata())
        ["envoy.tracers.opentelemetry.resource_typed_metadata"] = packed_metadata_2;
  });

  initialize();

  // Request 1
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers_1{
      {":method", "GET"}, {":path", "/route1"}, {":scheme", "http"}, {":authority", "host"}};
  auto response_1 =
      sendRequestAndWaitForResponse(request_headers_1, 0, default_response_headers_, 0, 0);
  ASSERT_TRUE(response_1->waitForEndStream());
  EXPECT_TRUE(response_1->complete());
  EXPECT_EQ("200", response_1->headers().getStatusValue());

  ASSERT_TRUE(grpc_receiver_upstream_->waitForHttpConnection(*dispatcher_, connection_));
  FakeStreamPtr stream_1;
  ASSERT_TRUE(connection_->waitForNewStream(*dispatcher_, stream_1, timeout));
  ExportTraceServiceRequest req_1;
  ASSERT_TRUE(stream_1->waitForGrpcMessage(*dispatcher_, req_1, timeout));

  bool found_1 = false;
  for (const auto& attribute : req_1.resource_spans(0).resource().attributes()) {
    if (attribute.key() == "route" && attribute.value().string_value() == "1") {
      found_1 = true;
      break;
    }
  }
  EXPECT_TRUE(found_1);

  // Clean up first stream
  stream_1->startGrpcStream();
  ExportTraceServiceResponse resp;
  stream_1->sendGrpcMessage(resp);
  stream_1->finishGrpcStream(Grpc::Status::WellKnownGrpcStatus::Ok);
  ASSERT_TRUE(stream_1->waitForEndStream(*dispatcher_, timeout));

  Http::TestRequestHeaderMapImpl request_headers_2{
      {":method", "GET"}, {":path", "/route2"}, {":scheme", "http"}, {":authority", "host"}};
  auto response_2 =
      sendRequestAndWaitForResponse(request_headers_2, 0, default_response_headers_, 0, 0);
  ASSERT_TRUE(response_2->waitForEndStream());
  EXPECT_TRUE(response_2->complete());
  EXPECT_EQ("200", response_2->headers().getStatusValue());

  FakeStreamPtr stream_2;
  ASSERT_TRUE(connection_->waitForNewStream(*dispatcher_, stream_2, timeout));
  ExportTraceServiceRequest req_2;
  ASSERT_TRUE(stream_2->waitForGrpcMessage(*dispatcher_, req_2, timeout));

  bool found_2 = false;
  for (const auto& attribute : req_2.resource_spans(0).resource().attributes()) {
    if (attribute.key() == "route" && attribute.value().string_value() == "2") {
      found_2 = true;
      break;
    }
  }
  EXPECT_TRUE(found_2);
  // Clean up second stream
  stream_2->startGrpcStream();
  stream_2->sendGrpcMessage(resp);
  stream_2->finishGrpcStream(Grpc::Status::WellKnownGrpcStatus::Ok);
  ASSERT_TRUE(stream_2->waitForEndStream(*dispatcher_, timeout));

  // Clean up connection and cluster
  cleanup();

  ASSERT_TRUE(connection_->close());
  ASSERT_TRUE(connection_->waitForDisconnect());
  connection_.reset();
}

} // namespace
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
