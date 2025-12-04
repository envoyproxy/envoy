#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

class DynamicModuleClusterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleClusterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }

  void initializeCluster(const std::string& cluster_name, const std::string& config) {
    // Set the search path for dynamic modules.
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);

    config_helper_.addConfigModifier(
        [cluster_name, config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          // Remove the default static cluster.
          bootstrap.mutable_static_resources()->mutable_clusters()->Clear();

          // Add a dynamic module cluster.
          auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
          cluster->set_name("cluster_0");
          cluster->mutable_connect_timeout()->set_seconds(5);
          // Use CLUSTER_PROVIDED since the dynamic module provides its own load balancer.
          cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

          envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig cluster_config;
          cluster_config.mutable_dynamic_module_config()->set_name("cluster_test");
          cluster_config.set_cluster_name(cluster_name);

          if (!config.empty()) {
            Protobuf::StringValue config_value;
            config_value.set_value(config);
            cluster_config.mutable_cluster_config()->PackFrom(config_value);
          }

          cluster->mutable_cluster_type()->set_name("envoy.clusters.dynamic_modules");
          cluster->mutable_cluster_type()->mutable_typed_config()->PackFrom(cluster_config);
        });

    HttpIntegrationTest::initialize();
  }

  ~DynamicModuleClusterIntegrationTest() override {
    TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleClusterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test that the dynamic module cluster can be created and initialized.
// This is a basic sanity test that verifies the module loads and initializes correctly.
TEST_P(DynamicModuleClusterIntegrationTest, BasicInitialization) {
  // Initialize the cluster with no initial hosts.
  initializeCluster("round_robin", "");

  // If we get here without crashing, the cluster initialized successfully.
  // The cluster has no hosts, so we can't make requests, but we verify the setup works.
  SUCCEED();
}

} // namespace Envoy
