#include "common/common/version.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"

#include "test/integration/http_integration.h"

#include "api/metrics_service.pb.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace Metrics {

class MetricsServiceIntegrationTest : public HttpIntegrationTest,
                                      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  MetricsServiceIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
  }

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::api::v2::Bootstrap& bootstrap) {
      auto* metrics_service_cluster = bootstrap.mutable_static_resources()->add_clusters();
      metrics_service_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      metrics_service_cluster->set_name("metrics_service");
      metrics_service_cluster->mutable_http2_protocol_options();

      auto* metrics_sink = bootstrap.add_stats_sinks();
      // metrics_sink->MergeFrom(bootstrap.stat_sinks()[0]);
      metrics_sink->set_name("envoy.metrics_service");
      envoy::api::v2::MetricsServiceConfig config;
      config.set_cluster_name("metrics_service");
      MessageUtil::jsonConvert(config, *metrics_sink->mutable_config());

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
    std::cout << "waitForMetricsRequest"
              << "\n";
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

INSTANTIATE_TEST_CASE_P(IpVersions, MetricsServiceIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

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

  cleanup();
}

} // namespace Metrics
} // namespace Stats
} // namespace Envoy