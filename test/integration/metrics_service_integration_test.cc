#include "common/common/version.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"

#include "api/metrics_service.pb.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace Metrics {

class MetricsServiceIntegrationTest : public HttpIntegrationTest,
                                      public Grpc::GrpcClientIntegrationParamTest {
public:
  MetricsServiceIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::api::v2::Bootstrap& bootstrap) {
      // metrics_service cluster for Envoy gRPC.
      auto* metrics_service_cluster = bootstrap.mutable_static_resources()->add_clusters();
      metrics_service_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      metrics_service_cluster->set_name("metrics_service");
      metrics_service_cluster->mutable_http2_protocol_options();
      // metrics_service gRPC service definition.
      auto* metrics_sink = bootstrap.add_stats_sinks();
      metrics_sink->set_name("envoy.metrics_service");
      envoy::api::v2::MetricsServiceConfig config;
      setGrpcService(*config.mutable_grpc_service(), "metrics_service",
                     fake_upstreams_.back()->localAddress());
      MessageUtil::jsonConvert(config, *metrics_sink->mutable_config());
      // Shrink reporting period down to 1s to make test not take forever.
      bootstrap.mutable_stats_flush_interval()->CopyFrom(
          Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    });

    HttpIntegrationTest::initialize();
    fake_upstreams_[1]->set_allow_unexpected_disconnects(true);
  }

  void waitForMetricsServiceConnection() {
    fake_metrics_service_connection_ = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);
  }

  void waitForMetricsStream() {
    metrics_service_request_ = fake_metrics_service_connection_->waitForNewStream(*dispatcher_);
  }

  void waitForMetricsRequest() {
    envoy::api::v2::StreamMetricsMessage request_msg;
    metrics_service_request_->waitForGrpcMessage(*dispatcher_, request_msg);
    EXPECT_STREQ("POST", metrics_service_request_->headers().Method()->value().c_str());
    EXPECT_STREQ("/envoy.api.v2.MetricsService/StreamMetrics",
                 metrics_service_request_->headers().Path()->value().c_str());
    EXPECT_STREQ("application/grpc",
                 metrics_service_request_->headers().ContentType()->value().c_str());
    EXPECT_TRUE(request_msg.envoy_metrics_size() > 0);
    const Protobuf::RepeatedPtrField<::io::prometheus::client::MetricFamily>& envoy_metrics =
        request_msg.envoy_metrics();
    bool known_counter_exists = false;
    bool known_gauge_exists = false;
    for (::io::prometheus::client::MetricFamily metrics_family : envoy_metrics) {
      if (metrics_family.name() == "cluster.cluster_0.membership_change" &&
          metrics_family.metric(0).has_counter()) {
        known_counter_exists = true;
        EXPECT_EQ(1, metrics_family.metric(0).counter().value());
      }
      if (metrics_family.name() == "cluster.cluster_0.membership_total" &&
          metrics_family.metric(0).has_gauge()) {
        known_gauge_exists = true;
        EXPECT_EQ(1, metrics_family.metric(0).gauge().value());
      }
      if (known_counter_exists && known_gauge_exists) {
        break;
      }
    }
    EXPECT_TRUE(known_counter_exists);
    EXPECT_TRUE(known_gauge_exists);
  }

  void cleanup() {
    if (fake_metrics_service_connection_ != nullptr) {
      fake_metrics_service_connection_->close();
      fake_metrics_service_connection_->waitForDisconnect();
    }
  }

  FakeHttpConnectionPtr fake_metrics_service_connection_;
  FakeStreamPtr metrics_service_request_;
};

INSTANTIATE_TEST_CASE_P(IpVersionsClientType, MetricsServiceIntegrationTest,
                        GRPC_CLIENT_INTEGRATION_PARAMS);

// Test a basic full access logging flow.
TEST_P(MetricsServiceIntegrationTest, BasicFlow) {
  initialize();
  waitForMetricsServiceConnection();
  waitForMetricsStream();
  waitForMetricsRequest();
  // Send an empty response and end the stream. This should never happen but make sure nothing
  // breaks and we make a new stream on a follow up request.
  metrics_service_request_->startGrpcStream();
  envoy::api::v2::StreamMetricsResponse response_msg;
  metrics_service_request_->sendGrpcMessage(response_msg);
  metrics_service_request_->finishGrpcStream(Grpc::Status::Ok);
  switch (clientType()) {
  case Grpc::ClientType::EnvoyGrpc:
    test_server_->waitForGaugeEq("cluster.metrics_service.upstream_rq_active", 0);
    break;
  case Grpc::ClientType::GoogleGrpc:
    test_server_->waitForCounterGe("grpc.metrics_service.streams_closed_0", 1);
    break;
  default:
    NOT_REACHED;
  }
  cleanup();
}

} // namespace Metrics
} // namespace Stats
} // namespace Envoy
