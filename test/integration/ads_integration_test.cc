#include "common/config/resources.h"
#include "common/protobuf/utility.h"

#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"

#include "api/cds.pb.h"
#include "api/discovery.pb.h"
#include "api/eds.pb.h"
#include "api/lds.pb.h"
#include "api/rds.pb.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class AdsIntegrationTest : public BaseIntegrationTest,
                           public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AdsIntegrationTest() : BaseIntegrationTest(GetParam()) {}

  void SetUp() override {
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
    registerPort("endpoint", fake_upstreams_.back()->localAddress()->ip()->port());
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
    registerPort("ads_upstream", fake_upstreams_.back()->localAddress()->ip()->port());
    createTestServer("test/config/integration/server_ads.yaml", {"http"});
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void expectDiscoveryRequest(const std::string& type_url, const std::string& version) {
    envoy::api::v2::DiscoveryRequest discovery_request;
    ads_stream_->waitForGrpcMessage(*dispatcher_, discovery_request);
    EXPECT_EQ(type_url, discovery_request.type_url());
    EXPECT_TRUE(discovery_request.resource_names().empty());
    EXPECT_EQ(version, discovery_request.version_info());
  }

  void sendDiscoveryResponse(const std::string& type_url, const Protobuf::Message& message,
                             const std::string& version) {
    envoy::api::v2::DiscoveryResponse discovery_response;
    discovery_response.set_version_info(version);
    discovery_response.set_type_url(type_url);
    discovery_response.add_resources()->PackFrom(message);
    ads_stream_->sendGrpcMessage(discovery_response);
  }

  envoy::api::v2::Cluster buildCluster(const std::string& name) {
    envoy::api::v2::Cluster cluster;
    cluster.set_name(name);
    cluster.mutable_connect_timeout()->set_seconds(5);
    cluster.set_type(envoy::api::v2::Cluster::EDS);
    cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_ads();
    cluster.set_lb_policy(envoy::api::v2::Cluster::ROUND_ROBIN);
    cluster.mutable_http2_protocol_options();
    return cluster;
  }

  envoy::api::v2::ClusterLoadAssignment buildClusterLoadAssignment(const std::string& name) {
    envoy::api::v2::ClusterLoadAssignment cluster_load_assignment;
    cluster_load_assignment.set_cluster_name(name);
    auto* endpoint = cluster_load_assignment.mutable_endpoints()
                         ->Add()
                         ->mutable_lb_endpoints()
                         ->Add()
                         ->mutable_endpoint();
    auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
    socket_address->set_address(Network::Test::getLoopbackAddressString(GetParam()));
    socket_address->set_port_value(fake_upstreams_[0]->localAddress()->ip()->port());
    return cluster_load_assignment;
  }

  envoy::api::v2::Listener buildListener(const std::string& name, const std::string& route_config) {
    envoy::api::v2::Listener listener;
    listener.set_name(name);
    auto* listener_socket_addr = listener.mutable_address()->mutable_socket_address();
    listener_socket_addr->set_address(Network::Test::getLoopbackAddressString(GetParam()));
    listener_socket_addr->set_port_value(0);
    auto* hcm_filter = listener.mutable_filter_chains()->Add()->mutable_filters()->Add();
    hcm_filter->set_name("envoy.http_connection_manager");
    envoy::api::v2::filter::HttpConnectionManager hcm_config;
    hcm_config.set_codec_type(envoy::api::v2::filter::HttpConnectionManager::HTTP2);
    auto* rds = hcm_config.mutable_rds();
    rds->set_route_config_name(route_config);
    rds->mutable_config_source()->mutable_ads();
    auto* router_filter = hcm_config.mutable_http_filters()->Add();
    router_filter->set_name("envoy.router");
    (*router_filter->mutable_config()->mutable_fields())["deprecated_v1"].set_bool_value(true);
    MessageUtil::jsonConvert(hcm_config, *hcm_filter->mutable_config());
    return listener;
  }

  envoy::api::v2::RouteConfiguration buildRouteConfig(const std::string& name,
                                                      const std::string& cluster) {
    envoy::api::v2::RouteConfiguration route_config;
    route_config.set_name(name);
    auto* virtual_host = route_config.mutable_virtual_hosts()->Add();
    virtual_host->set_name("integration");
    virtual_host->add_domains("*");
    auto* route = virtual_host->mutable_routes()->Add();
    route->mutable_match()->set_prefix("/");
    route->mutable_route()->set_cluster(cluster);
    return route_config;
  }

  FakeHttpConnectionPtr ads_connection_;
  FakeStreamPtr ads_stream_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, AdsIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(AdsIntegrationTest, Basic) {
  ads_connection_ = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);
  ads_stream_ = ads_connection_->waitForNewStream();
  ads_stream_->startGrpcStream();

  expectDiscoveryRequest(Config::TypeUrl::get().Cluster, "");
  sendDiscoveryResponse(Config::TypeUrl::get().Cluster, buildCluster("cluster_0"), "1");

  expectDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "");
  sendDiscoveryResponse(Config::TypeUrl::get().ClusterLoadAssignment,
                        buildClusterLoadAssignment("cluster_0"), "1");

  expectDiscoveryRequest(Config::TypeUrl::get().Cluster, "1");
  expectDiscoveryRequest(Config::TypeUrl::get().Listener, "");

  sendDiscoveryResponse(Config::TypeUrl::get().Listener,
                        buildListener("listener_0", "route_config_0"), "1");

  expectDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1");
  expectDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "");
  sendDiscoveryResponse(Config::TypeUrl::get().RouteConfiguration,
                        buildRouteConfig("route_config_0", "cluster_0"), "1");

  expectDiscoveryRequest(Config::TypeUrl::get().Listener, "1");
  expectDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1");

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
}

} // namespace
} // namespace Envoy
