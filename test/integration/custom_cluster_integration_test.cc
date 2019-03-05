#include "envoy/api/v2/eds.pb.h"

#include "common/network/address_impl.h"
#include "common/upstream/load_balancer_impl.h"

#include "test/config/utility.h"
#include "test/integration/clusters/cluster_factory_config.pb.h"
#include "test/integration/clusters/custom_static_cluster.h"
#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

const int UpstreamIndex = 0;
const std::string CUSTOM_CLUSTER_CONFIG = R"EOF(
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    name: cluster_0
    cluster_type:
        name: envoy.clusters.custom_static
        typed_config:
          "@type": type.googleapis.com/test.integration.clusters.CustomStaticConfig
          priority: 10
          address: 127.0.0.1
          port_value: 0
  listeners:
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: envoy.http_connection_manager
        config:
          stat_prefix: config_test
          http_filters:
            name: envoy.router
          codec_type: HTTP1
          route_config:
            virtual_hosts:
              name: integration
              routes:
                route:
                  cluster: cluster_0
                match:
                  prefix: "/"
              domains: "*"
            name: route_config_0
)EOF";

// Integration test for cluster extension using CustomStaticCluster.
class CustomClusterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public HttpIntegrationTest {
public:
  CustomClusterIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam(), realTime(),
                            CUSTOM_CLUSTER_CONFIG) {}

  void initialize() override {
    setUpstreamCount(1);
    // update the address and port in the cluster config
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);

      test::integration::clusters::CustomStaticConfig config;
      config.set_priority(10);
      config.set_address(Network::Test::getLoopbackAddressString(ipVersion()));
      config.set_port_value(fake_upstreams_[UpstreamIndex]->localAddress()->ip()->port());

      cluster_0->mutable_cluster_type()->mutable_typed_config()->PackFrom(config);
    });
    HttpIntegrationTest::initialize();
    test_server_->waitForGaugeGe("cluster_manager.active_clusters", 1);
  }

  Network::Address::IpVersion ipVersion() const { return version_; }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, CustomClusterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(CustomClusterIntegrationTest, TestRouterHeaderOnly) {
  testRouterHeaderOnlyRequestAndResponse(nullptr, UpstreamIndex);
}

TEST_P(CustomClusterIntegrationTest, TestTwoRequests) { testTwoRequests(false); }

TEST_P(CustomClusterIntegrationTest, TestCustomConfig) {
  // Calls our initialize(), which includes establishing a listener, route, and cluster.
  initialize();

  // Verify the cluster is correctly setup with the custom priority
  const auto& cluster_map = test_server_->server().clusterManager().clusters();
  EXPECT_EQ(1, cluster_map.size());
  EXPECT_EQ(1, cluster_map.count("cluster_0"));
  const auto& cluster_ref = cluster_map.find("cluster_0")->second;
  const auto& hostset_per_priority = cluster_ref.get().prioritySet().hostSetsPerPriority();
  EXPECT_EQ(11, hostset_per_priority.size());
  const Envoy::Upstream::HostSetPtr& host_set = hostset_per_priority[10];
  EXPECT_EQ(1, host_set->hosts().size());
  EXPECT_EQ(1, host_set->healthyHosts().size());
  EXPECT_EQ(10, host_set->priority());
}
} // namespace
} // namespace Envoy
