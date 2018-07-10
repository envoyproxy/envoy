#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/endpoint/endpoint.pb.h"
#include "envoy/service/discovery/v2/hds.pb.h"
#include "envoy/upstream/upstream.h"

#include "common/config/metadata.h"
#include "common/config/resources.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"
#include "common/upstream/health_checker_impl.h"
#include "common/upstream/health_discovery_service.h"

#include "test/common/upstream/utility.h"
#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class HdsIntegrationTest : public HttpIntegrationTest,
                           public testing::TestWithParam<Network::Address::IpVersion> {
public:
  HdsIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

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
      hds_confid->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("hds_delegate");
      auto* hds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      hds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      hds_cluster->mutable_circuit_breakers()->Clear();
      hds_cluster->set_name("hds_delegate");
      hds_cluster->mutable_http2_protocol_options();
      // Switch predefined cluster_0 to EDS filesystem sourcing.
      // TODO(lilika): Remove eds dependency
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cluster_0->mutable_hosts()->Clear();
      cluster_0->set_type(envoy::api::v2::Cluster::EDS);
      auto* eds_cluster_config = cluster_0->mutable_eds_cluster_config();
      eds_cluster_config->mutable_eds_config()->set_path(eds_helper_.eds_path());
    });
    HttpIntegrationTest::initialize();
    hds_upstream_ = fake_upstreams_[0].get();
    host_upstream_ = new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_);
    for (uint32_t i = 0; i < upstream_endpoints_ - 2; ++i) {
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
    test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);
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
  FakeUpstream* host_upstream_{};
  FakeUpstream* service_upstream_[upstream_endpoints_]{};
  uint32_t hds_requests_{};
  EdsHelper eds_helper_;
  FakeHttpConnectionPtr fake_fake_connection;
  FakeStreamPtr fake_fake_stream;
};

INSTANTIATE_TEST_CASE_P(IpVersions, HdsIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

// Test setup of Envoy, the Server, and a host
TEST_P(HdsIntegrationTest, Setup) {
  initialize();

  // Messages
  envoy::service::discovery::v2::HealthCheckRequest envoy_msg;
  envoy::service::discovery::v2::HealthCheckRequest envoy_msg_2;

  // HealthCheckSpecifier
  envoy::service::discovery::v2::HealthCheckSpecifier server_health_check_specifier;
  server_health_check_specifier.mutable_interval()->set_nanos(500000000); // 500ms
  auto* endpoint_address = server_health_check_specifier.add_health_check()
                               ->add_endpoints()
                               ->add_endpoints()
                               ->mutable_address()
                               ->mutable_socket_address();
  endpoint_address->set_address(Network::Test::getLoopbackAddressString(
      version_)); // fake_upstreams_[2]->localAddress()->ip()->addressAsString());
  endpoint_address->set_port_value(host_upstream_->localAddress()->ip()->port());

  auto* anna = server_health_check_specifier.mutable_health_check(0)->mutable_endpoints(0);
  anna->mutable_locality()->set_region("some_region");
  anna->mutable_locality()->set_zone("some_zone");
  anna->mutable_locality()->set_sub_zone("crete");

  // Server <--> Envoy
  fake_hds_connection_ = hds_upstream_->waitForHttpConnection(*dispatcher_);
  hds_stream_ = fake_hds_connection_->waitForNewStream(*dispatcher_);
  hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg);

  EXPECT_EQ(0, test_server_->counter("hds_delegate.requests")->value());
  EXPECT_EQ(1, test_server_->counter("hds_delegate.responses")->value());

  // Server asks for healthchecking
  hds_stream_->startGrpcStream();
  hds_stream_->sendGrpcMessage(server_health_check_specifier);

  EXPECT_EQ(host_upstream_->httpType(), FakeHttpConnection::Type::HTTP1);
  fake_fake_connection = host_upstream_->waitForHttpConnection(*dispatcher_);

  test_server_->waitForCounterGe("hds_delegate.requests", ++hds_requests_);
  hds_stream_->waitForGrpcMessage(*dispatcher_, envoy_msg_2);
  fake_fake_stream = fake_fake_connection->waitForNewStream(*dispatcher_);
  fake_fake_stream->waitForEndStream(*dispatcher_);

  EXPECT_STREQ(fake_fake_stream->headers().Path()->value().c_str() , "/healthcheck");
  EXPECT_STREQ(fake_fake_stream->headers().Method()->value().c_str(), "GET");
  test_server_->waitForCounterGe("hds_delegate.responses", 2);

  // Endpoint reponds to the healthcheck
  fake_fake_stream->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  fake_fake_stream->encodeData(1024, true);


  // Clean up connections
  fake_fake_connection->close();
  fake_fake_connection->waitForDisconnect();
  cleanupHdsConnection();

  EXPECT_EQ(1, test_server_->counter("hds_delegate.requests")->value());
  EXPECT_EQ(2, test_server_->counter("hds_delegate.responses")->value());
}

} // namespace
} // namespace Envoy
