#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/endpoint/endpoint.pb.h"
#include "envoy/service/discovery/v2/hds.pb.h"

#include "common/config/resources.h"

#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class HdsIntegrationTest : public HttpIntegrationTest,
                           public testing::TestWithParam<Network::Address::IpVersion> {
public:
  HdsIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  // Used as args to updateClusterLocalityAssignment().
  struct LocalityAssignment {
    LocalityAssignment() : LocalityAssignment({}, 0) {}
    LocalityAssignment(const std::vector<uint32_t>& endpoints) : LocalityAssignment(endpoints, 0) {}
    LocalityAssignment(const std::vector<uint32_t>& endpoints, uint32_t weight)
        : endpoints_(endpoints), weight_(weight) {}

    // service_upstream_ indices for endpoints in the cluster.
    const std::vector<uint32_t> endpoints_;
    // If non-zero, locality level weighting.
    const uint32_t weight_{};
  };

  void createUpstreams() override {
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
    hds_upstream_ = fake_upstreams_.back().get();
    HttpIntegrationTest::createUpstreams();
  }

  void initialize() override {
    setUpstreamCount(upstream_endpoints_);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      // Setup hds and corresponding gRPC cluster.
      auto* hds_confid = bootstrap.mutable_hds_config();
      hds_confid->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
      hds_confid->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("hds_report");
      auto* hds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      hds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      hds_cluster->mutable_circuit_breakers()->Clear();
      hds_cluster->set_name("hds_report");
      hds_cluster->mutable_http2_protocol_options();
      // Put ourselves in a locality that will be used in
      // updateClusterLoadAssignment()
      auto* locality = bootstrap.mutable_node()->mutable_locality();
      locality->set_region("some_region");
      locality->set_zone("zone_name");
      locality->set_sub_zone(sub_zone_);
      // Switch predefined cluster_0 to EDS filesystem sourcing.
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cluster_0->mutable_hosts()->Clear();
      cluster_0->set_type(envoy::api::v2::Cluster::EDS);
      auto* eds_cluster_config = cluster_0->mutable_eds_cluster_config();
      eds_cluster_config->mutable_eds_config()->set_path(eds_helper_.eds_path());
    });
    HttpIntegrationTest::initialize();
    hds_upstream_ = fake_upstreams_[0].get();
    for (uint32_t i = 0; i < upstream_endpoints_; ++i) {
      service_upstream_[i] = fake_upstreams_[i + 1].get();
    }
  }

  void waitForHdsStream() {
    fake_hds_connection_ = hds_upstream_->waitForHttpConnection(*dispatcher_);
    hds_stream_ = fake_hds_connection_->waitForNewStream(*dispatcher_);
  }

  void requestHealthCheckSpecifier() {
    envoy::service::discovery::v2::HealthCheckSpecifier server_health_check_specifier;
    server_health_check_specifier.mutable_interval()->set_nanos(500000000); // 500ms

    hds_stream_->sendGrpcMessage(server_health_check_specifier);
    // Wait until the request has been received by Envoy.
    test_server_->waitForCounterGe("hds_reporter.requests", ++hds_requests_);
  }

  void cleanupUpstreamConnection() {
    codec_client_->close();
    if (fake_upstream_connection_ != nullptr) {
      fake_upstream_connection_->close();
      fake_upstream_connection_->waitForDisconnect();
    }
  }

  void cleanupHdsConnection() {
    if (fake_hds_connection_ != nullptr) {
      fake_hds_connection_->close();
      fake_hds_connection_->waitForDisconnect();
    }
  }

  static constexpr uint32_t upstream_endpoints_ = 5;

  IntegrationStreamDecoderPtr response_;
  std::string sub_zone_{"winter"};
  FakeHttpConnectionPtr fake_hds_connection_;
  FakeStreamPtr hds_stream_;
  FakeUpstream* hds_upstream_{};
  FakeUpstream* service_upstream_[upstream_endpoints_]{};
  uint32_t hds_requests_{};
  EdsHelper eds_helper_;
  bool locality_weighted_lb_{};

  const uint64_t request_size_ = 1024;
  const uint64_t response_size_ = 512;
};

INSTANTIATE_TEST_CASE_P(IpVersions, HdsIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

// Test connectivity of Envoy and the Server
TEST_P(HdsIntegrationTest, Simple) {
  initialize();
  envoy::service::discovery::v2::HealthCheckRequest envoy_msg;
  envoy::service::discovery::v2::HealthCheckRequest envoy_msg_2;
  envoy::service::discovery::v2::HealthCheckSpecifier server_health_check_specifier;
  server_health_check_specifier.mutable_interval()->set_nanos(500000000); // 500ms

  // Server <--> Envoy
  fake_hds_connection_ = hds_upstream_->waitForHttpConnection(*dispatcher_);
  hds_stream_ = fake_hds_connection_->waitForNewStream(*dispatcher_);
  hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg);

  EXPECT_EQ(0, test_server_->counter("hds_reporter.requests")->value());
  EXPECT_EQ(1, test_server_->counter("hds_reporter.responses")->value());

  // Send a message to Envoy, and wait until it's received
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier);
  test_server_->waitForCounterGe("hds_reporter.requests", ++hds_requests_);

  // Wait for Envoy to reply
  hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_2);

  EXPECT_EQ(1, test_server_->counter("hds_reporter.requests")->value());
  EXPECT_EQ(2, test_server_->counter("hds_reporter.responses")->value());

  cleanupHdsConnection();
}

} // namespace
} // namespace Envoy
