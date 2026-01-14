#include <filesystem>

#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

class DynamicModuleClusterIntegrationTest : public testing::TestWithParam<std::string>,
                                            public HttpIntegrationTest {
public:
  DynamicModuleClusterIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {}

  void SetUp() override {
    // Set the module search path before initialization.
    std::string shared_object_path =
        Envoy::Extensions::DynamicModules::testSharedObjectPath("cluster_test", GetParam());
    std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      cluster->set_name("dynamic_module_cluster");
      cluster->mutable_connect_timeout()->set_seconds(5);
      cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

      auto* typed_extension_config = cluster->mutable_cluster_type();
      typed_extension_config->set_name("envoy.clusters.dynamic_modules");

      envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig cluster_config;
      cluster_config.mutable_dynamic_module_config()->set_name("cluster_test");
      cluster_config.set_cluster_name("round_robin");
      cluster_config.set_dynamic_host_discovery(true);
      cluster_config.mutable_cleanup_interval()->set_seconds(5);
      cluster_config.set_max_hosts(1024);

      typed_extension_config->mutable_typed_config()->PackFrom(cluster_config);
    });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(
    Languages, DynamicModuleClusterIntegrationTest, testing::Values("rust"),
    Envoy::Extensions::DynamicModules::DynamicModuleTestLanguages::languageParamToTestName);

// Test basic cluster initialization.
TEST_P(DynamicModuleClusterIntegrationTest, BasicInitialization) {
  initialize();
  // If we get here without crashing, the cluster was initialized successfully.
  SUCCEED();
}

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
