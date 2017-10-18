#include "common/config/resources.h"
#include "common/protobuf/utility.h"

#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "api/cds.pb.h"
#include "api/discovery.pb.h"
#include "api/eds.pb.h"
#include "api/lds.pb.h"
#include "api/rds.pb.h"
#include "gtest/gtest.h"

using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;

namespace Envoy {
namespace {

class AdsIntegrationTest : public HttpIntegrationTest,
                           public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AdsIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

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

  AssertionResult compareDiscoveryRequest(const std::string& expected_type_url,
                                          const std::string& expected_version,
                                          const std::vector<std::string>& expected_resource_names) {
    envoy::api::v2::DiscoveryRequest discovery_request;
    ads_stream_->waitForGrpcMessage(*dispatcher_, discovery_request);
    // TODO(PiotrSikora): Remove this hack once fixed internally.
    if (!(expected_type_url == discovery_request.type_url())) {
      return AssertionFailure() << fmt::format("type_url {} does not match expected {}",
                                               discovery_request.type_url(), expected_type_url);
    }
    const std::vector<std::string> resource_names(discovery_request.resource_names().cbegin(),
                                                  discovery_request.resource_names().cend());
    if (expected_resource_names != resource_names) {
      return AssertionFailure() << fmt::format(
                 "resources {} do not match expected {} in {}",
                 fmt::join(resource_names.begin(), resource_names.end(), ","),
                 fmt::join(expected_resource_names.begin(), expected_resource_names.end(), ","),
                 discovery_request.DebugString());
    }
    // TODO(PiotrSikora): Remove this hack once fixed internally.
    if (!(expected_version == discovery_request.version_info())) {
      return AssertionFailure() << fmt::format("version {} does not match expected {} in {}",
                                               discovery_request.version_info(), expected_version,
                                               discovery_request.DebugString());
    }
    return AssertionSuccess();
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
    return TestUtility::parseYaml<envoy::api::v2::Cluster>(fmt::format(R"EOF(
      name: {}
      connect_timeout: 5s
      type: EDS
      eds_cluster_config: {{ eds_config: {{ ads: {{}} }} }}
      lb_policy: ROUND_ROBIN
      http2_protocol_options: {{}}
    )EOF",
                                                                       name));
  }

  envoy::api::v2::ClusterLoadAssignment buildClusterLoadAssignment(const std::string& name) {
    return TestUtility::parseYaml<envoy::api::v2::ClusterLoadAssignment>(
        fmt::format(R"EOF(
      cluster_name: {}
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: {}
                port_value: {}
    )EOF",
                    name, Network::Test::getLoopbackAddressString(GetParam()),
                    fake_upstreams_[0]->localAddress()->ip()->port()));
  }

  envoy::api::v2::Listener buildListener(const std::string& name, const std::string& route_config) {
    return TestUtility::parseYaml<envoy::api::v2::Listener>(
        fmt::format(R"EOF(
      name: {}
      address:
        socket_address:
          address: {}
          port_value: 0
      filter_chains:
        filters:
        - name: envoy.http_connection_manager
          config:
            codec_type: HTTP2
            rds:
              route_config_name: {}
              config_source: {{ ads: {{}} }}
            http_filters: [{{ name: envoy.router, config: {{ deprecated_v1: true }}}}]
    )EOF",
                    name, Network::Test::getLoopbackAddressString(GetParam()), route_config));
  }

  envoy::api::v2::RouteConfiguration buildRouteConfig(const std::string& name,
                                                      const std::string& cluster) {
    return TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(fmt::format(R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
    )EOF",
                                                                                  name, cluster));
  }

  void makeSingleRequest() {
    registerTestServerPorts({"http"});
    testRouterHeaderOnlyRequestAndResponse(true);
    cleanupUpstreamAndDownstream();
    fake_upstream_connection_ = nullptr;
  }

  void initialize() override {
    BaseIntegrationTest::initialize();
    ads_connection_ = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);
    ads_stream_ = ads_connection_->waitForNewStream(*dispatcher_);
    ads_stream_->startGrpcStream();
  }

  FakeHttpConnectionPtr ads_connection_;
  FakeStreamPtr ads_stream_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, AdsIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Validate basic config delivery and upgrade.
TEST_P(AdsIntegrationTest, Basic) {
  initialize();

  // Send initial configuration, validate we can process a request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}));
  sendDiscoveryResponse(Config::TypeUrl::get().Cluster, buildCluster("cluster_0"), "1");

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "", {"cluster_0"}));
  sendDiscoveryResponse(Config::TypeUrl::get().ClusterLoadAssignment,
                        buildClusterLoadAssignment("cluster_0"), "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}));
  sendDiscoveryResponse(Config::TypeUrl::get().Listener,
                        buildListener("listener_0", "route_config_0"), "1");

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1", {"cluster_0"}));
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "", {"route_config_0"}));
  sendDiscoveryResponse(Config::TypeUrl::get().RouteConfiguration,
                        buildRouteConfig("route_config_0", "cluster_0"), "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}));
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1", {"route_config_0"}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  makeSingleRequest();

  // Upgrade RDS/CDS/EDS to a newer config, validate we can process a request.
  sendDiscoveryResponse(Config::TypeUrl::get().Cluster, buildCluster("cluster_1"), "2");
  sendDiscoveryResponse(Config::TypeUrl::get().ClusterLoadAssignment,
                        buildClusterLoadAssignment("cluster_1"), "2");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1",
                                      {"cluster_1", "cluster_0"}));
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1", {"cluster_1"}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "2", {}));
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "2", {"cluster_1"}));
  sendDiscoveryResponse(Config::TypeUrl::get().RouteConfiguration,
                        buildRouteConfig("route_config_0", "cluster_1"), "2");
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "2", {"route_config_0"}));

  makeSingleRequest();

  // Upgrade LDS/RDS, validate we can process a request.
  sendDiscoveryResponse(Config::TypeUrl::get().Listener,
                        buildListener("listener_1", "route_config_1"), "2");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "2",
                                      {"route_config_1", "route_config_0"}));
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "2", {}));
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "2", {"route_config_1"}));
  sendDiscoveryResponse(Config::TypeUrl::get().RouteConfiguration,
                        buildRouteConfig("route_config_1", "cluster_1"), "3");
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "3", {"route_config_1"}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 2);
  makeSingleRequest();
}

// Validate that we can recover from failures.
TEST_P(AdsIntegrationTest, Failure) {
  initialize();

  // Send initial configuration, failing each xDS once (via a type mismatch), validate we can
  // process a request.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}));
  sendDiscoveryResponse(Config::TypeUrl::get().Cluster, buildClusterLoadAssignment("cluster_0"),
                        "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}));

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}));
  sendDiscoveryResponse(Config::TypeUrl::get().Cluster, buildCluster("cluster_0"), "1");

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "", {"cluster_0"}));
  sendDiscoveryResponse(Config::TypeUrl::get().ClusterLoadAssignment, buildCluster("cluster_0"),
                        "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "1", {}));
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "", {"cluster_0"}));
  sendDiscoveryResponse(Config::TypeUrl::get().ClusterLoadAssignment,
                        buildClusterLoadAssignment("cluster_0"), "1");

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "1", {"cluster_0"}));
  sendDiscoveryResponse(Config::TypeUrl::get().Listener,
                        buildRouteConfig("listener_0", "route_config_0"), "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "", {}));
  sendDiscoveryResponse(Config::TypeUrl::get().Listener,
                        buildListener("listener_0", "route_config_0"), "1");

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "", {"route_config_0"}));
  sendDiscoveryResponse(Config::TypeUrl::get().RouteConfiguration,
                        buildListener("route_config_0", "cluster_0"), "1");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Listener, "1", {}));
  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "", {"route_config_0"}));
  sendDiscoveryResponse(Config::TypeUrl::get().RouteConfiguration,
                        buildRouteConfig("route_config_0", "cluster_0"), "1");

  EXPECT_TRUE(
      compareDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "1", {"route_config_0"}));

  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  makeSingleRequest();
}

} // namespace
} // namespace Envoy
