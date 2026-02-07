#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.h"

#include "source/extensions/clusters/dynamic_modules/cluster.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

// Test peer class to access private members of DynamicModuleCluster.
// This must be outside the anonymous namespace to match the friend declaration.
class DynamicModuleClusterTestPeer {
public:
  static envoy_dynamic_module_type_cluster_module_ptr
  getInModuleCluster(const DynamicModuleCluster& cluster) {
    return cluster.in_module_cluster_;
  }

  static size_t getHostMapSize(DynamicModuleCluster& cluster) {
    absl::ReaderMutexLock lock(&cluster.host_map_lock_);
    return cluster.host_map_.size();
  }
};

namespace {

class DynamicModuleClusterTest : public testing::Test {
public:
  DynamicModuleClusterTest() {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);
  }

  absl::StatusOr<std::pair<Upstream::ClusterSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
  createCluster(const std::string& yaml_config) {
    envoy::config::cluster::v3::Cluster cluster_config =
        Upstream::parseClusterFromV3Yaml(yaml_config);
    Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr, false);
    DynamicModuleClusterFactory factory;
    return factory.create(cluster_config, factory_context);
  }

  std::string makeYamlConfig(const std::string& module_name,
                             const std::string& cluster_name = "test") {
    return fmt::format(R"EOF(
name: test_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.dynamic_modules
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
    dynamic_module_config:
      name: {}
    cluster_name: {}
)EOF",
                       module_name, cluster_name);
  }

  std::string makeYamlConfigWithClusterConfig(const std::string& module_name,
                                              const std::string& cluster_name,
                                              const std::string& cluster_config) {
    return fmt::format(R"EOF(
name: test_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.dynamic_modules
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
    dynamic_module_config:
      name: {}
    cluster_name: {}
    cluster_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: {}
)EOF",
                       module_name, cluster_name, cluster_config);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
};

// Test that creating a cluster with a valid no-op module succeeds.
TEST_F(DynamicModuleClusterTest, BasicCreation) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_NE(nullptr, result->first);
  EXPECT_NE(nullptr, result->second);
}

// Test that creating a cluster with cluster_config succeeds.
TEST_F(DynamicModuleClusterTest, CreationWithClusterConfig) {
  auto result =
      createCluster(makeYamlConfigWithClusterConfig("cluster_no_op", "test", "some_config"));
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_NE(nullptr, result->first);
  EXPECT_NE(nullptr, result->second);
}

// Test that a non-CLUSTER_PROVIDED lb_policy is rejected.
TEST_F(DynamicModuleClusterTest, InvalidLbPolicy) {
  const std::string yaml = R"EOF(
name: test_cluster
connect_timeout: 0.25s
lb_policy: ROUND_ROBIN
cluster_type:
  name: envoy.clusters.dynamic_modules
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
    dynamic_module_config:
      name: cluster_no_op
    cluster_name: test
)EOF";

  auto result = createCluster(yaml);
  ASSERT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("CLUSTER_PROVIDED"));
}

// Test that a missing module fails gracefully.
TEST_F(DynamicModuleClusterTest, MissingModule) {
  auto result = createCluster(makeYamlConfig("nonexistent_module"));
  ASSERT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Failed to load dynamic module"));
}

// Test that on_cluster_config_new returning nullptr fails.
TEST_F(DynamicModuleClusterTest, ConfigNewFail) {
  auto result = createCluster(makeYamlConfig("cluster_config_new_fail"));
  ASSERT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Failed to create in-module cluster configuration"));
}

// Test that on_cluster_new returning nullptr fails.
TEST_F(DynamicModuleClusterTest, ClusterNewFail) {
  auto result = createCluster(makeYamlConfig("cluster_new_fail"));
  ASSERT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Failed to create in-module cluster instance"));
}

// Test host addition and removal via the public API.
TEST_F(DynamicModuleClusterTest, HostManagement) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Initially no hosts in the map.
  EXPECT_EQ(0, DynamicModuleClusterTestPeer::getHostMapSize(*cluster));

  // Add a host.
  auto host1 = cluster->addHost("127.0.0.1:10001", 1);
  ASSERT_NE(nullptr, host1);
  EXPECT_EQ(1, DynamicModuleClusterTestPeer::getHostMapSize(*cluster));

  // Add another host.
  auto host2 = cluster->addHost("127.0.0.1:10002", 2);
  ASSERT_NE(nullptr, host2);
  EXPECT_EQ(2, DynamicModuleClusterTestPeer::getHostMapSize(*cluster));

  // Find a host by raw pointer.
  EXPECT_EQ(host1, cluster->findHost(host1.get()));
  EXPECT_EQ(host2, cluster->findHost(host2.get()));
  EXPECT_EQ(nullptr, cluster->findHost(reinterpret_cast<void*>(0xDEAD)));

  // Remove the first host.
  EXPECT_TRUE(cluster->removeHost(host1));
  EXPECT_EQ(1, DynamicModuleClusterTestPeer::getHostMapSize(*cluster));

  // Removing an already-removed host returns false.
  EXPECT_FALSE(cluster->removeHost(host1));

  // Removing a nullptr returns false.
  EXPECT_FALSE(cluster->removeHost(nullptr));

  // Remove the second host.
  EXPECT_TRUE(cluster->removeHost(host2));
  EXPECT_EQ(0, DynamicModuleClusterTestPeer::getHostMapSize(*cluster));
}

// Test that invalid addresses are rejected.
TEST_F(DynamicModuleClusterTest, InvalidAddress) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  EXPECT_EQ(nullptr, cluster->addHost("invalid_address", 1));
  EXPECT_EQ(nullptr, cluster->addHost("", 1));
}

// Test that invalid weights are rejected.
TEST_F(DynamicModuleClusterTest, InvalidWeight) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  EXPECT_EQ(nullptr, cluster->addHost("127.0.0.1:10001", 0));
  EXPECT_EQ(nullptr, cluster->addHost("127.0.0.1:10001", 129));
}

// Test the load balancer lifecycle.
TEST_F(DynamicModuleClusterTest, LoadBalancerLifecycle) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  auto& talb = result->second;
  ASSERT_NE(nullptr, talb);
  EXPECT_TRUE(talb->initialize().ok());

  auto lb_factory = talb->factory();
  ASSERT_NE(nullptr, lb_factory);

  NiceMock<Upstream::MockPrioritySet> mock_ps;
  auto lb = lb_factory->create({mock_ps});
  ASSERT_NE(nullptr, lb);

  // No host added, so chooseHost returns nullptr.
  auto host_response = lb->chooseHost(nullptr);
  EXPECT_EQ(nullptr, host_response.host);

  // peekAnotherHost should return nullptr (not supported).
  EXPECT_EQ(nullptr, lb->peekAnotherHost(nullptr));

  // selectExistingConnection should return nullopt (not supported).
  std::vector<uint8_t> hash_key;
  auto dummy_host = cluster->addHost("127.0.0.1:10099", 1);
  ASSERT_NE(nullptr, dummy_host);
  auto existing = lb->selectExistingConnection(nullptr, *dummy_host, hash_key);
  EXPECT_FALSE(existing.has_value());

  // lifetimeCallbacks should return empty (not supported).
  EXPECT_FALSE(lb->lifetimeCallbacks().has_value());

  // Clean up.
  cluster->removeHost(dummy_host);
}

// Test the load balancer with hosts and the priority set access.
TEST_F(DynamicModuleClusterTest, LoadBalancerWithHosts) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Add hosts before creating the LB.
  auto host1 = cluster->addHost("127.0.0.1:10001", 1);
  ASSERT_NE(nullptr, host1);
  auto host2 = cluster->addHost("127.0.0.1:10002", 2);
  ASSERT_NE(nullptr, host2);

  // Create the LB.
  auto& talb = result->second;
  ASSERT_NE(nullptr, talb);
  EXPECT_TRUE(talb->initialize().ok());

  auto lb_factory = talb->factory();
  ASSERT_NE(nullptr, lb_factory);
  NiceMock<Upstream::MockPrioritySet> mock_ps;
  auto lb = lb_factory->create({mock_ps});
  ASSERT_NE(nullptr, lb);

  // Verify priority set access via the LB.
  auto* dm_lb = dynamic_cast<DynamicModuleLoadBalancer*>(lb.get());
  ASSERT_NE(nullptr, dm_lb);
  const auto& ps = dm_lb->prioritySet();
  EXPECT_EQ(1, ps.hostSetsPerPriority().size());
  EXPECT_EQ(2, ps.hostSetsPerPriority()[0]->healthyHosts().size());
}

// Test that the cluster initializes with the Primary phase.
TEST_F(DynamicModuleClusterTest, InitializePhase) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);
  EXPECT_EQ(Upstream::Cluster::InitializePhase::Primary, cluster->initializePhase());
}

// Test that the cluster can be initialized and the in-module cluster is set.
TEST_F(DynamicModuleClusterTest, InModuleClusterIsSet) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // The in-module cluster pointer should be set by the factory.
  EXPECT_NE(nullptr, DynamicModuleClusterTestPeer::getInModuleCluster(*cluster));
}

// Test the ABI callback implementations for host management directly.
TEST_F(DynamicModuleClusterTest, AbiCallbacksHostManagement) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Test add_host callback.
  std::string addr1 = "127.0.0.1:10001";
  envoy_dynamic_module_type_module_buffer addr_buf = {addr1.data(), addr1.size()};
  auto* host_ptr = envoy_dynamic_module_callback_cluster_add_host(cluster.get(), addr_buf, 1);
  EXPECT_NE(nullptr, host_ptr);

  // Test add_host with invalid address.
  std::string bad_addr = "invalid";
  envoy_dynamic_module_type_module_buffer bad_buf = {bad_addr.data(), bad_addr.size()};
  EXPECT_EQ(nullptr, envoy_dynamic_module_callback_cluster_add_host(cluster.get(), bad_buf, 1));

  // Test add_host with invalid weight.
  std::string addr2 = "127.0.0.1:10002";
  envoy_dynamic_module_type_module_buffer addr_buf2 = {addr2.data(), addr2.size()};
  EXPECT_EQ(nullptr, envoy_dynamic_module_callback_cluster_add_host(cluster.get(), addr_buf2, 0));

  // Test remove_host callback.
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_remove_host(cluster.get(), host_ptr));
  // Removing again should fail.
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_remove_host(cluster.get(), host_ptr));
}

// Test the LB ABI callback implementations directly.
TEST_F(DynamicModuleClusterTest, LbAbiCallbacks) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Add some hosts.
  auto host1 = cluster->addHost("127.0.0.1:10001", 1);
  ASSERT_NE(nullptr, host1);
  auto host2 = cluster->addHost("127.0.0.1:10002", 2);
  ASSERT_NE(nullptr, host2);

  // Create the LB.
  auto& talb = result->second;
  EXPECT_TRUE(talb->initialize().ok());
  auto lb_factory = talb->factory();
  NiceMock<Upstream::MockPrioritySet> mock_ps;
  auto lb = lb_factory->create({mock_ps});
  auto* dm_lb = dynamic_cast<DynamicModuleLoadBalancer*>(lb.get());
  ASSERT_NE(nullptr, dm_lb);

  // Test get_healthy_host_count.
  EXPECT_EQ(2, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(dm_lb, 0));
  // Invalid priority returns 0.
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(dm_lb, 99));

  // Test get_healthy_host.
  auto* returned_host0 = envoy_dynamic_module_callback_cluster_lb_get_healthy_host(dm_lb, 0, 0);
  EXPECT_EQ(host1.get(), returned_host0);
  auto* returned_host1 = envoy_dynamic_module_callback_cluster_lb_get_healthy_host(dm_lb, 0, 1);
  EXPECT_EQ(host2.get(), returned_host1);

  // Out of bounds index.
  EXPECT_EQ(nullptr, envoy_dynamic_module_callback_cluster_lb_get_healthy_host(dm_lb, 0, 2));
  // Invalid priority.
  EXPECT_EQ(nullptr, envoy_dynamic_module_callback_cluster_lb_get_healthy_host(dm_lb, 99, 0));
}

// Test that a module missing cluster symbols fails with an error.
TEST_F(DynamicModuleClusterTest, MissingClusterSymbol) {
  // The "no_op" module exports on_program_init but not cluster symbols.
  auto result = createCluster(makeYamlConfig("no_op"));
  ASSERT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("envoy_dynamic_module_on_cluster_config_new"));
}

// Test that creating a cluster with BytesValue config type works.
TEST_F(DynamicModuleClusterTest, CreationWithBytesValueConfig) {
  const std::string yaml = R"EOF(
name: test_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.dynamic_modules
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
    dynamic_module_config:
      name: cluster_no_op
    cluster_name: test
    cluster_config:
      "@type": type.googleapis.com/google.protobuf.BytesValue
      value: "c29tZV9ieXRlcw=="
)EOF";

  auto result = createCluster(yaml);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_NE(nullptr, result->first);
}

// Test that creating a cluster with Struct config type works.
TEST_F(DynamicModuleClusterTest, CreationWithStructConfig) {
  const std::string yaml = R"EOF(
name: test_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.dynamic_modules
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
    dynamic_module_config:
      name: cluster_no_op
    cluster_name: test
    cluster_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        key1: value1
        key2: value2
)EOF";

  auto result = createCluster(yaml);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_NE(nullptr, result->first);
}

// Test that creating a cluster with an unknown Any type uses raw serialized bytes.
TEST_F(DynamicModuleClusterTest, CreationWithUnknownAnyConfig) {
  const std::string yaml = R"EOF(
name: test_cluster
connect_timeout: 0.25s
lb_policy: CLUSTER_PROVIDED
cluster_type:
  name: envoy.clusters.dynamic_modules
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
    dynamic_module_config:
      name: cluster_no_op
    cluster_name: test
    cluster_config:
      "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
      dynamic_module_config:
        name: nested
      cluster_name: inner
)EOF";

  auto result = createCluster(yaml);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_NE(nullptr, result->first);
}

// Test that initialize() triggers startPreInit which calls on_cluster_init, and
// preInitComplete triggers onPreInitComplete which invokes the completion callback.
TEST_F(DynamicModuleClusterTest, PreInitFlow) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Add a host before initialization.
  auto host = cluster->addHost("127.0.0.1:10001", 1);
  ASSERT_NE(nullptr, host);

  // Call initialize() which sets initialization_complete_callback_ and calls startPreInit().
  // The no-op module's on_cluster_init does nothing, so we can then call preInitComplete.
  bool initialized = false;
  cluster->initialize([&initialized] {
    initialized = true;
    return absl::OkStatus();
  });

  // Now call preInitComplete via the ABI callback to complete the initialization flow.
  envoy_dynamic_module_callback_cluster_pre_init_complete(cluster.get());
  EXPECT_TRUE(initialized);
}

// Test the DynamicModuleClusterHandle destructor dispatches to main thread.
TEST_F(DynamicModuleClusterTest, HandleDestructorDispatchesToMainThread) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Create a handle and let it go out of scope.
  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
  // The handle destructor should post to the dispatcher. The mock dispatcher will execute
  // the posted callback inline.
  handle.reset();
}

} // namespace
} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
