#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/endpoint/endpoint.pb.h"
//#include "envoy/api/v2/endpoint/load_report.pb.h"
#include "envoy/service/discovery/v2/hds.pb.h"

#include "common/config/resources.h"

#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class HDSIntegrationTest : public HttpIntegrationTest,
                                 public testing::TestWithParam<Network::Address::IpVersion> {
public:
  HDSIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {
    // We rely on some fairly specific load balancing picks in this test, so
    // determinizie the schedule.
    setDeterministic();
  }

  void addEndpoint(envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoints,
                   uint32_t index, uint32_t& num_endpoints) {
    setUpstreamAddress(index + 1, *locality_lb_endpoints.add_lb_endpoints());
    ++num_endpoints;
  }

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

  // We need to supply the endpoints via EDS to provide locality information for
  // load reporting. Use a filesystem delivery to simplify test mechanics.
  void updateClusterLoadAssignment(const LocalityAssignment& winter_upstreams,
                                   const LocalityAssignment& dragon_upstreams,
                                   const LocalityAssignment& p1_winter_upstreams,
                                   const LocalityAssignment& p1_dragon_upstreams) {
    uint32_t num_endpoints = 0;
    envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
    cluster_load_assignment.set_cluster_name("cluster_0");

    auto* winter = cluster_load_assignment.add_endpoints();
    winter->mutable_locality()->set_region("some_region");
    winter->mutable_locality()->set_zone("zone_name");
    winter->mutable_locality()->set_sub_zone("winter");
    if (winter_upstreams.weight_ > 0) {
      winter->mutable_load_balancing_weight()->set_value(winter_upstreams.weight_);
    }
    for (uint32_t index : winter_upstreams.endpoints_) {
      addEndpoint(*winter, index, num_endpoints);
    }

    auto* dragon = cluster_load_assignment.add_endpoints();
    dragon->mutable_locality()->set_region("some_region");
    dragon->mutable_locality()->set_zone("zone_name");
    dragon->mutable_locality()->set_sub_zone("dragon");
    if (dragon_upstreams.weight_ > 0) {
      dragon->mutable_load_balancing_weight()->set_value(dragon_upstreams.weight_);
    }
    for (uint32_t index : dragon_upstreams.endpoints_) {
      addEndpoint(*dragon, index, num_endpoints);
    }

    auto* winter_p1 = cluster_load_assignment.add_endpoints();
    winter_p1->set_priority(1);
    winter_p1->mutable_locality()->set_region("some_region");
    winter_p1->mutable_locality()->set_zone("zone_name");
    winter_p1->mutable_locality()->set_sub_zone("winter");
    for (uint32_t index : p1_winter_upstreams.endpoints_) {
      addEndpoint(*winter_p1, index, num_endpoints);
    }

    auto* dragon_p1 = cluster_load_assignment.add_endpoints();
    dragon_p1->set_priority(1);
    dragon_p1->mutable_locality()->set_region("some_region");
    dragon_p1->mutable_locality()->set_zone("zone_name");
    dragon_p1->mutable_locality()->set_sub_zone("dragon");
    for (uint32_t index : p1_dragon_upstreams.endpoints_) {
      addEndpoint(*dragon_p1, index, num_endpoints);
    }
    eds_helper_.setEds({cluster_load_assignment}, *test_server_);
  }

  void createUpstreams() override {
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
    hds_upstream_ = fake_upstreams_.back().get();
    HttpIntegrationTest::createUpstreams();
  }

  void initialize() override {
    setUpstreamCount(upstream_endpoints_);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      // Setup load reporting and corresponding gRPC cluster.
      auto* hds_confid = bootstrap.mutable_cluster_manager()->mutable_hds_config();
      hds_confid->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
      hds_confid->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("load_report");
      auto* hds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      hds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      hds_cluster->mutable_circuit_breakers()->Clear();
      hds_cluster->set_name("load_report");
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
      if (locality_weighted_lb_) {
        cluster_0->mutable_common_lb_config()->mutable_locality_weighted_lb_config();
      }
    });
    HttpIntegrationTest::initialize();
    hds_upstream_ = fake_upstreams_[0].get();
    for (uint32_t i = 0; i < upstream_endpoints_; ++i) {
      service_upstream_[i] = fake_upstreams_[i + 1].get();
    }
    updateClusterLoadAssignment({}, {}, {}, {});
  }

  void initiateClientConnection() {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestHeaderMapImpl headers{{":method", "POST"},       {":path", "/test/long/url"},
                                    {":scheme", "http"},       {":authority", "host"},
                                    {"x-lyft-user-id", "123"}, {"x-forwarded-for", "10.0.0.1"}};
    response_ = codec_client_->makeRequestWithBody(headers, request_size_);
  }

  void waitForHDSStream() {
    fake_hds_connection_ = hds_upstream_->waitForHttpConnection(*dispatcher_);
    hds_stream_ = fake_hds_connection_->waitForNewStream(*dispatcher_);
  }


  void waitForUpstreamResponse(uint32_t endpoint_index, uint32_t response_code = 200) {
    fake_upstream_connection_ =
        service_upstream_[endpoint_index]->waitForHttpConnection(*dispatcher_);
    upstream_request_ = fake_upstream_connection_->waitForNewStream(*dispatcher_);
    upstream_request_->waitForEndStream(*dispatcher_);

    upstream_request_->encodeHeaders(
        Http::TestHeaderMapImpl{{":status", std::to_string(response_code)}}, false);
    upstream_request_->encodeData(response_size_, true);
    response_->waitForEndStream();

    ASSERT_TRUE(upstream_request_->complete());
    EXPECT_EQ(request_size_, upstream_request_->bodyLength());

    ASSERT_TRUE(response_->complete());
    EXPECT_STREQ(std::to_string(response_code).c_str(),
                 response_->headers().Status()->value().c_str());
    EXPECT_EQ(response_size_, response_->body().size());
  }

  void cleanupUpstreamConnection() {
    codec_client_->close();
    if (fake_upstream_connection_ != nullptr) {
      fake_upstream_connection_->close();
      fake_upstream_connection_->waitForDisconnect();
    }
  }

  void cleanupHDSConnection() {
    if (fake_hds_connection_ != nullptr) {
      fake_hds_connection_->close();
      fake_hds_connection_->waitForDisconnect();
    }
  }


void requestHealthCheckSpecifier() {
    envoy::service::discovery::v2::HealthCheckSpecifier server_health_check_specifier;
    server_health_check_specifier.mutable_interval()->set_nanos(500000000); // 500ms
    //for (const auto& cluster : clusters) {
    //  server_health_check_specifier.add_clusters(cluster);
    //}
    hds_stream_->sendGrpcMessage(server_health_check_specifier);
    // Wait until the request has been received by Envoy.
    test_server_->waitForCounterGe("load_reporter.requests", ++hds_requests_);
  }





  void sendAndReceiveUpstream(uint32_t endpoint_index, uint32_t response_code = 200) {
    initiateClientConnection();
    waitForUpstreamResponse(endpoint_index, response_code);
    cleanupUpstreamConnection();
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

INSTANTIATE_TEST_CASE_P(IpVersions, HDSIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);


// Send and Receive a message
TEST_P(HDSIntegrationTest, Simple) {
  initialize();

  waitForHDSStream();
  hds_stream_->startGrpcStream();
  requestHealthCheckSpecifier();

  initiateClientConnection();

  cleanupUpstreamConnection();
  cleanupHDSConnection();
  //EXPECT_EQ(1,0);
}


} // namespace
} // namespace Envoy
