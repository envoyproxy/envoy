#include <atomic>
#include <thread>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.h"

#include "source/common/config/metadata.h"
#include "source/common/http/message_impl.h"
#include "source/extensions/clusters/dynamic_modules/cluster.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/environment.h"
#include "test/test_common/thread_factory_for_test.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
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
    absl::ReaderMutexLock lock(cluster.host_map_lock_);
    return cluster.host_map_.size();
  }

  static void clearInModuleCluster(DynamicModuleCluster& cluster) {
    cluster.in_module_cluster_ = nullptr;
  }
};

using ::testing::_;
using ::testing::Return;

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

  // Re-opens stat creation so tests can call `define_*` from the test thread.
  static void unfreezeStatCreation(DynamicModuleClusterConfig& config) {
    config.stat_creation_frozen_ = false;
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

// Convenience wrapper to add hosts without locality (passes empty locality and metadata vectors).
bool addSimpleHosts(DynamicModuleCluster& cluster, const std::vector<std::string>& addresses,
                    const std::vector<uint32_t>& weights,
                    std::vector<Upstream::HostSharedPtr>& result_hosts, uint32_t priority = 0) {
  std::vector<std::string> empty_strings(addresses.size());
  return cluster.addHosts(addresses, weights, empty_strings, empty_strings, empty_strings, {},
                          result_hosts, priority);
}

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

// Test batch host addition and removal via the public API.
TEST_F(DynamicModuleClusterTest, HostManagement) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Initially no hosts in the map.
  EXPECT_EQ(0, DynamicModuleClusterTestPeer::getHostMapSize(*cluster));

  // Add two hosts in a single batch.
  std::vector<std::string> addresses = {"127.0.0.1:10001", "127.0.0.1:10002"};
  std::vector<uint32_t> weights = {1, 2};
  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, addresses, weights, hosts));
  EXPECT_EQ(2, hosts.size());
  EXPECT_EQ(2, DynamicModuleClusterTestPeer::getHostMapSize(*cluster));

  // Find hosts by raw pointer.
  EXPECT_EQ(hosts[0], cluster->findHost(hosts[0].get()));
  EXPECT_EQ(hosts[1], cluster->findHost(hosts[1].get()));
  EXPECT_EQ(nullptr, cluster->findHost(reinterpret_cast<void*>(0xDEAD)));

  // Remove the first host.
  EXPECT_EQ(1, cluster->removeHosts({hosts[0]}));
  EXPECT_EQ(1, DynamicModuleClusterTestPeer::getHostMapSize(*cluster));

  // Removing an already-removed host returns 0.
  EXPECT_EQ(0, cluster->removeHosts({hosts[0]}));

  // Removing a nullptr returns 0.
  EXPECT_EQ(0, cluster->removeHosts({nullptr}));

  // Remove the second host.
  EXPECT_EQ(1, cluster->removeHosts({hosts[1]}));
  EXPECT_EQ(0, DynamicModuleClusterTestPeer::getHostMapSize(*cluster));
}

// Test batch addition with a single host.
TEST_F(DynamicModuleClusterTest, HostManagementSingleHost) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  std::vector<std::string> addresses = {"127.0.0.1:10001"};
  std::vector<uint32_t> weights = {1};
  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, addresses, weights, hosts));
  EXPECT_EQ(1, hosts.size());
  EXPECT_EQ(1, DynamicModuleClusterTestPeer::getHostMapSize(*cluster));

  EXPECT_EQ(1, cluster->removeHosts({hosts[0]}));
  EXPECT_EQ(0, DynamicModuleClusterTestPeer::getHostMapSize(*cluster));
}

// Test that batch remove of multiple hosts works correctly.
TEST_F(DynamicModuleClusterTest, BatchRemoveMultipleHosts) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  std::vector<std::string> addresses = {"127.0.0.1:10001", "127.0.0.1:10002", "127.0.0.1:10003"};
  std::vector<uint32_t> weights = {1, 2, 3};
  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, addresses, weights, hosts));
  EXPECT_EQ(3, hosts.size());

  // Remove all three at once.
  EXPECT_EQ(3, cluster->removeHosts(hosts));
  EXPECT_EQ(0, DynamicModuleClusterTestPeer::getHostMapSize(*cluster));
}

// Test that invalid addresses cause the entire batch to fail.
TEST_F(DynamicModuleClusterTest, InvalidAddress) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  std::vector<Upstream::HostSharedPtr> hosts;

  // Single invalid address.
  EXPECT_FALSE(addSimpleHosts(*cluster, {"invalid_address"}, {1}, hosts));
  EXPECT_FALSE(addSimpleHosts(*cluster, {""}, {1}, hosts));

  // Mixed valid and invalid addresses: the entire batch should fail.
  EXPECT_FALSE(addSimpleHosts(*cluster, {"127.0.0.1:10001", "invalid_address"}, {1, 1}, hosts));
  EXPECT_EQ(0, DynamicModuleClusterTestPeer::getHostMapSize(*cluster));
}

// Test that invalid weights cause the entire batch to fail.
TEST_F(DynamicModuleClusterTest, InvalidWeight) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  std::vector<Upstream::HostSharedPtr> hosts;

  EXPECT_FALSE(addSimpleHosts(*cluster, {"127.0.0.1:10001"}, {0}, hosts));
  EXPECT_FALSE(addSimpleHosts(*cluster, {"127.0.0.1:10001"}, {129}, hosts));

  // Mixed valid and invalid weights: the entire batch should fail.
  EXPECT_FALSE(addSimpleHosts(*cluster, {"127.0.0.1:10001", "127.0.0.1:10002"}, {1, 0}, hosts));
  EXPECT_EQ(0, DynamicModuleClusterTestPeer::getHostMapSize(*cluster));
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
  std::vector<Upstream::HostSharedPtr> dummy_hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10099"}, {1}, dummy_hosts));
  ASSERT_EQ(1, dummy_hosts.size());
  auto existing = lb->selectExistingConnection(nullptr, *dummy_hosts[0], hash_key);
  EXPECT_FALSE(existing.has_value());

  // lifetimeCallbacks should return empty (not supported).
  EXPECT_FALSE(lb->lifetimeCallbacks().has_value());

  // Clean up.
  cluster->removeHosts(dummy_hosts);
}

// Test the load balancer with hosts and the priority set access.
TEST_F(DynamicModuleClusterTest, LoadBalancerWithHosts) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Add hosts before creating the LB.
  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001", "127.0.0.1:10002"}, {1, 2}, hosts));
  ASSERT_EQ(2, hosts.size());

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

// Test the ABI callback implementations for batch host management directly.
TEST_F(DynamicModuleClusterTest, AbiCallbacksHostManagement) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Test add_hosts callback with two hosts.
  std::string addr1 = "127.0.0.1:10001";
  std::string addr2 = "127.0.0.1:10002";
  envoy_dynamic_module_type_module_buffer addr_bufs[] = {{addr1.data(), addr1.size()},
                                                         {addr2.data(), addr2.size()}};
  uint32_t weights[] = {1, 2};
  envoy_dynamic_module_type_module_buffer empty_loc[] = {{"", 0}, {"", 0}};
  envoy_dynamic_module_type_cluster_host_envoy_ptr host_ptrs[2] = {nullptr, nullptr};
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_add_hosts(cluster.get(), 0, addr_bufs, weights,
                                                              empty_loc, empty_loc, empty_loc,
                                                              nullptr, 0, 2, host_ptrs));
  EXPECT_NE(nullptr, host_ptrs[0]);
  EXPECT_NE(nullptr, host_ptrs[1]);

  // Test add_hosts with invalid address causes entire batch to fail.
  std::string bad_addr = "invalid";
  envoy_dynamic_module_type_module_buffer bad_bufs[] = {{bad_addr.data(), bad_addr.size()}};
  uint32_t bad_weights[] = {1};
  envoy_dynamic_module_type_module_buffer empty_loc1[] = {{"", 0}};
  envoy_dynamic_module_type_cluster_host_envoy_ptr bad_host_ptrs[1] = {nullptr};
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_add_hosts(
      cluster.get(), 0, bad_bufs, bad_weights, empty_loc1, empty_loc1, empty_loc1, nullptr, 0, 1,
      bad_host_ptrs));

  // Test add_hosts with invalid weight causes entire batch to fail.
  std::string addr3 = "127.0.0.1:10003";
  envoy_dynamic_module_type_module_buffer addr_buf3[] = {{addr3.data(), addr3.size()}};
  uint32_t zero_weight[] = {0};
  envoy_dynamic_module_type_cluster_host_envoy_ptr zero_host_ptrs[1] = {nullptr};
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_add_hosts(
      cluster.get(), 0, addr_buf3, zero_weight, empty_loc1, empty_loc1, empty_loc1, nullptr, 0, 1,
      zero_host_ptrs));

  // Test remove_hosts callback.
  EXPECT_EQ(2, envoy_dynamic_module_callback_cluster_remove_hosts(cluster.get(), host_ptrs, 2));
  // Removing again should return 0.
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_remove_hosts(cluster.get(), host_ptrs, 2));
}

// Test the LB ABI callback implementations directly.
TEST_F(DynamicModuleClusterTest, LbAbiCallbacks) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Add some hosts.
  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001", "127.0.0.1:10002"}, {1, 2}, hosts));
  ASSERT_EQ(2, hosts.size());

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
  EXPECT_EQ(hosts[0].get(), returned_host0);
  auto* returned_host1 = envoy_dynamic_module_callback_cluster_lb_get_healthy_host(dm_lb, 0, 1);
  EXPECT_EQ(hosts[1].get(), returned_host1);

  // Out of bounds index.
  EXPECT_EQ(nullptr, envoy_dynamic_module_callback_cluster_lb_get_healthy_host(dm_lb, 0, 2));
  // Invalid priority.
  EXPECT_EQ(nullptr, envoy_dynamic_module_callback_cluster_lb_get_healthy_host(dm_lb, 99, 0));
}

// Test the LB host information ABI callbacks.
TEST_F(DynamicModuleClusterTest, LbHostInformationCallbacks) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Add hosts with different weights.
  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001", "127.0.0.1:10002", "127.0.0.1:10003"},
                             {10, 20, 30}, hosts));
  ASSERT_EQ(3, hosts.size());

  // Create the LB.
  auto& talb = result->second;
  EXPECT_TRUE(talb->initialize().ok());
  auto lb_factory = talb->factory();
  NiceMock<Upstream::MockPrioritySet> mock_ps;
  auto lb = lb_factory->create({mock_ps});
  auto* dm_lb = dynamic_cast<DynamicModuleLoadBalancer*>(lb.get());
  ASSERT_NE(nullptr, dm_lb);

  // Test get_cluster_name.
  envoy_dynamic_module_type_envoy_buffer name_buf = {};
  envoy_dynamic_module_callback_cluster_lb_get_cluster_name(dm_lb, &name_buf);
  EXPECT_GT(name_buf.length, 0);

  // Test get_hosts_count.
  EXPECT_EQ(3, envoy_dynamic_module_callback_cluster_lb_get_hosts_count(dm_lb, 0));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_hosts_count(dm_lb, 99));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_hosts_count(nullptr, 0));

  // Test get_healthy_host_count (existing).
  EXPECT_EQ(3, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(dm_lb, 0));

  // Test get_degraded_hosts_count.
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_degraded_hosts_count(dm_lb, 0));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_degraded_hosts_count(dm_lb, 99));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_degraded_hosts_count(nullptr, 0));

  // Test get_priority_set_size.
  EXPECT_EQ(1, envoy_dynamic_module_callback_cluster_lb_get_priority_set_size(dm_lb));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_priority_set_size(nullptr));

  // Test get_healthy_host_address.
  envoy_dynamic_module_type_envoy_buffer addr_buf = {};
  EXPECT_TRUE(
      envoy_dynamic_module_callback_cluster_lb_get_healthy_host_address(dm_lb, 0, 0, &addr_buf));
  EXPECT_EQ("127.0.0.1:10001", std::string(addr_buf.ptr, addr_buf.length));
  // Out of bounds.
  EXPECT_FALSE(
      envoy_dynamic_module_callback_cluster_lb_get_healthy_host_address(dm_lb, 0, 99, &addr_buf));
  EXPECT_FALSE(
      envoy_dynamic_module_callback_cluster_lb_get_healthy_host_address(dm_lb, 99, 0, &addr_buf));
  EXPECT_FALSE(
      envoy_dynamic_module_callback_cluster_lb_get_healthy_host_address(nullptr, 0, 0, &addr_buf));

  // Test get_healthy_host_weight.
  EXPECT_EQ(10, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight(dm_lb, 0, 0));
  EXPECT_EQ(20, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight(dm_lb, 0, 1));
  EXPECT_EQ(30, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight(dm_lb, 0, 2));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight(dm_lb, 0, 99));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight(nullptr, 0, 0));

  // Test get_host_address.
  envoy_dynamic_module_type_envoy_buffer host_addr_buf = {};
  EXPECT_TRUE(
      envoy_dynamic_module_callback_cluster_lb_get_host_address(dm_lb, 0, 1, &host_addr_buf));
  EXPECT_EQ("127.0.0.1:10002", std::string(host_addr_buf.ptr, host_addr_buf.length));
  // Out of bounds.
  EXPECT_FALSE(
      envoy_dynamic_module_callback_cluster_lb_get_host_address(dm_lb, 0, 99, &host_addr_buf));
  EXPECT_FALSE(
      envoy_dynamic_module_callback_cluster_lb_get_host_address(nullptr, 0, 0, &host_addr_buf));

  // Test get_host_weight.
  EXPECT_EQ(10, envoy_dynamic_module_callback_cluster_lb_get_host_weight(dm_lb, 0, 0));
  EXPECT_EQ(20, envoy_dynamic_module_callback_cluster_lb_get_host_weight(dm_lb, 0, 1));
  EXPECT_EQ(30, envoy_dynamic_module_callback_cluster_lb_get_host_weight(dm_lb, 0, 2));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_host_weight(dm_lb, 0, 99));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_host_weight(nullptr, 0, 0));

  // Test get_host_health.
  EXPECT_EQ(envoy_dynamic_module_type_host_health_Healthy,
            envoy_dynamic_module_callback_cluster_lb_get_host_health(dm_lb, 0, 0));
  EXPECT_EQ(envoy_dynamic_module_type_host_health_Unhealthy,
            envoy_dynamic_module_callback_cluster_lb_get_host_health(dm_lb, 0, 99));
  EXPECT_EQ(envoy_dynamic_module_type_host_health_Unhealthy,
            envoy_dynamic_module_callback_cluster_lb_get_host_health(nullptr, 0, 0));

  // Test get_host_health_by_address.
  envoy_dynamic_module_type_host_health health_result;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address(
      nullptr, {nullptr, 0}, &health_result));
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address(
      dm_lb, {nullptr, 0}, &health_result));
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address(
      nullptr, {nullptr, 0}, nullptr));

  // Test get_host_stat with all enum values (counters and gauges).
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_host_stat(
                   dm_lb, 0, 0, envoy_dynamic_module_type_host_stat_CxConnectFail));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_host_stat(
                   dm_lb, 0, 0, envoy_dynamic_module_type_host_stat_CxTotal));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_host_stat(
                   dm_lb, 0, 0, envoy_dynamic_module_type_host_stat_RqError));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_host_stat(
                   dm_lb, 0, 0, envoy_dynamic_module_type_host_stat_RqSuccess));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_host_stat(
                   dm_lb, 0, 0, envoy_dynamic_module_type_host_stat_RqTimeout));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_host_stat(
                   dm_lb, 0, 0, envoy_dynamic_module_type_host_stat_RqTotal));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_host_stat(
                   dm_lb, 0, 0, envoy_dynamic_module_type_host_stat_CxActive));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_host_stat(
                   dm_lb, 0, 0, envoy_dynamic_module_type_host_stat_RqActive));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_host_stat(
                   dm_lb, 0, 99, envoy_dynamic_module_type_host_stat_RqTotal));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_host_stat(
                   nullptr, 0, 0, envoy_dynamic_module_type_host_stat_RqTotal));

  // Test get_host_locality.
  envoy_dynamic_module_type_envoy_buffer region = {}, zone = {}, sub_zone = {};
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_host_locality(dm_lb, 0, 0, &region,
                                                                         &zone, &sub_zone));
  // Default locality is empty.
  EXPECT_EQ(0, region.length);
  // Out of bounds.
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_locality(dm_lb, 0, 99, &region,
                                                                          &zone, &sub_zone));
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_locality(nullptr, 0, 0, &region,
                                                                          &zone, &sub_zone));
  // Null output parameters are allowed.
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_host_locality(dm_lb, 0, 0, nullptr,
                                                                         nullptr, nullptr));

  // Test set_host_data and get_host_data.
  uintptr_t data = 0;
  // Initially no data.
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_host_data(dm_lb, 0, 0, &data));
  EXPECT_EQ(0, data);
  // Set data.
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_set_host_data(dm_lb, 0, 0, 12345));
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_host_data(dm_lb, 0, 0, &data));
  EXPECT_EQ(12345, data);
  // Clear data by setting to 0.
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_set_host_data(dm_lb, 0, 0, 0));
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_host_data(dm_lb, 0, 0, &data));
  EXPECT_EQ(0, data);
  // Out of bounds.
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_set_host_data(dm_lb, 0, 99, 1));
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_data(dm_lb, 0, 99, &data));
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_set_host_data(nullptr, 0, 0, 1));
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_data(nullptr, 0, 0, &data));

  // Test get_host_metadata_string (no metadata set, should return false).
  envoy_dynamic_module_type_module_buffer filter_name = {"envoy.lb", 8};
  envoy_dynamic_module_type_module_buffer key = {"shard", 5};
  envoy_dynamic_module_type_envoy_buffer meta_result = {};
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string(
      dm_lb, 0, 0, filter_name, key, &meta_result));
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string(
      nullptr, 0, 0, filter_name, key, &meta_result));

  // Test get_host_metadata_number.
  double num_result = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_number(
      dm_lb, 0, 0, filter_name, key, &num_result));
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_number(
      nullptr, 0, 0, filter_name, key, &num_result));

  // Test get_host_metadata_bool.
  bool bool_result = false;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_bool(
      dm_lb, 0, 0, filter_name, key, &bool_result));
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_bool(
      nullptr, 0, 0, filter_name, key, &bool_result));

  // Test get_locality_count.
  EXPECT_GE(envoy_dynamic_module_callback_cluster_lb_get_locality_count(dm_lb, 0), 0);
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_locality_count(dm_lb, 99));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_locality_count(nullptr, 0));

  // Test get_locality_host_count.
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_locality_host_count(dm_lb, 0, 99));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_locality_host_count(nullptr, 0, 0));

  // Test get_locality_host_address.
  envoy_dynamic_module_type_envoy_buffer locality_addr = {};
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_locality_host_address(dm_lb, 0, 99, 0,
                                                                                  &locality_addr));
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_locality_host_address(nullptr, 0, 0, 0,
                                                                                  &locality_addr));

  // Test get_locality_weight.
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_locality_weight(dm_lb, 0, 99));
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_locality_weight(nullptr, 0, 0));
}

// Test the LB host information callbacks with hosts that have metadata and locality data.
// This exercises code paths not reachable via addHosts() which creates hosts without metadata
// and with empty locality.
TEST_F(DynamicModuleClusterTest, LbHostInformationWithMetadataAndLocality) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Build metadata with string, number, and bool fields under filter "envoy.lb".
  envoy::config::core::v3::Metadata metadata;
  Config::Metadata::mutableMetadataValue(metadata, "envoy.lb", "region_tag")
      .set_string_value("us-east-1");
  Config::Metadata::mutableMetadataValue(metadata, "envoy.lb", "shard_weight")
      .set_number_value(42.5);
  Config::Metadata::mutableMetadataValue(metadata, "envoy.lb", "is_canary").set_bool_value(true);

  // Build locality data.
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_region("us-east");
  zone_a.set_zone("us-east-1a");
  zone_a.set_sub_zone("rack-1");

  envoy::config::core::v3::Locality zone_b;
  zone_b.set_region("us-west");
  zone_b.set_zone("us-west-2b");
  zone_b.set_sub_zone("rack-2");

  // Create hosts with metadata and locality using the test helper.
  auto host_a1 =
      Upstream::makeTestHost(cluster->info(), "tcp://10.0.0.1:8080", metadata, zone_a, 10);
  auto host_a2 =
      Upstream::makeTestHost(cluster->info(), "tcp://10.0.0.2:8080", metadata, zone_a, 20);
  auto host_b1 =
      Upstream::makeTestHost(cluster->info(), "tcp://10.0.0.3:8080", metadata, zone_b, 30);

  // Group hosts by locality.
  Upstream::HostVector zone_a_hosts = {host_a1, host_a2};
  Upstream::HostVector zone_b_hosts = {host_b1};
  std::vector<Upstream::HostVector> locality_hosts = {zone_a_hosts, zone_b_hosts};
  auto hosts_per_locality =
      std::make_shared<Upstream::HostsPerLocalityImpl>(std::move(locality_hosts), false);

  // Build all-hosts vector.
  auto all_hosts =
      std::make_shared<Upstream::HostVector>(Upstream::HostVector{host_a1, host_a2, host_b1});

  // Set up locality weights.
  auto locality_weights =
      std::make_shared<const Upstream::LocalityWeights>(Upstream::LocalityWeights{50, 50});

  // Update the cluster's priority set directly with locality data.
  cluster->prioritySet().updateHosts(
      0, Upstream::HostSetImpl::partitionHosts(all_hosts, hosts_per_locality), locality_weights,
      {host_a1, host_a2, host_b1}, {}, absl::nullopt, absl::nullopt);

  // Create the LB.
  auto& talb = result->second;
  EXPECT_TRUE(talb->initialize().ok());
  auto lb_factory = talb->factory();
  NiceMock<Upstream::MockPrioritySet> mock_ps;
  auto lb = lb_factory->create({mock_ps});
  auto* dm_lb = dynamic_cast<DynamicModuleLoadBalancer*>(lb.get());
  ASSERT_NE(nullptr, dm_lb);

  // ---- Test get_cluster_name with nullptr lb. ----
  envoy_dynamic_module_type_envoy_buffer name_buf = {};
  envoy_dynamic_module_callback_cluster_lb_get_cluster_name(nullptr, &name_buf);
  EXPECT_EQ(nullptr, name_buf.ptr);
  EXPECT_EQ(0, name_buf.length);

  // ---- Test metadata string lookup (success path). ----
  envoy_dynamic_module_type_module_buffer filter_name = {"envoy.lb", 8};
  envoy_dynamic_module_type_module_buffer str_key = {"region_tag", 10};
  envoy_dynamic_module_type_envoy_buffer meta_result = {};
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string(
      dm_lb, 0, 0, filter_name, str_key, &meta_result));
  EXPECT_EQ("us-east-1", std::string(meta_result.ptr, meta_result.length));

  // Test metadata string with wrong key (key not found).
  envoy_dynamic_module_type_module_buffer bad_key = {"nonexistent", 11};
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string(
      dm_lb, 0, 0, filter_name, bad_key, &meta_result));

  // Test metadata string with wrong filter name (filter not found).
  envoy_dynamic_module_type_module_buffer bad_filter = {"wrong.filter", 12};
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string(
      dm_lb, 0, 0, bad_filter, str_key, &meta_result));

  // ---- Test metadata number lookup (success path). ----
  envoy_dynamic_module_type_module_buffer num_key = {"shard_weight", 12};
  double num_result = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_number(
      dm_lb, 0, 0, filter_name, num_key, &num_result));
  EXPECT_DOUBLE_EQ(42.5, num_result);

  // Test metadata number with a key that has a string value (type mismatch).
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_number(
      dm_lb, 0, 0, filter_name, str_key, &num_result));

  // ---- Test metadata bool lookup (success path). ----
  envoy_dynamic_module_type_module_buffer bool_key = {"is_canary", 9};
  bool bool_result = false;
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_bool(
      dm_lb, 0, 0, filter_name, bool_key, &bool_result));
  EXPECT_TRUE(bool_result);

  // Test metadata bool with a key that has a string value (type mismatch).
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_bool(
      dm_lb, 0, 0, filter_name, str_key, &bool_result));

  // ---- Test metadata with out-of-bounds priority and index. ----
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string(
      dm_lb, 99, 0, filter_name, str_key, &meta_result));
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string(
      dm_lb, 0, 99, filter_name, str_key, &meta_result));

  // ---- Test locality count. ----
  EXPECT_EQ(2, envoy_dynamic_module_callback_cluster_lb_get_locality_count(dm_lb, 0));

  // ---- Test locality host count. ----
  EXPECT_EQ(2, envoy_dynamic_module_callback_cluster_lb_get_locality_host_count(dm_lb, 0, 0));
  EXPECT_EQ(1, envoy_dynamic_module_callback_cluster_lb_get_locality_host_count(dm_lb, 0, 1));
  // Out of bounds locality index.
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_locality_host_count(dm_lb, 0, 99));
  // Out of bounds priority.
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_locality_host_count(dm_lb, 99, 0));

  // ---- Test locality host address (success path). ----
  envoy_dynamic_module_type_envoy_buffer locality_addr = {};
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_locality_host_address(dm_lb, 0, 0, 0,
                                                                                 &locality_addr));
  EXPECT_EQ("10.0.0.1:8080", std::string(locality_addr.ptr, locality_addr.length));

  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_locality_host_address(dm_lb, 0, 1, 0,
                                                                                 &locality_addr));
  EXPECT_EQ("10.0.0.3:8080", std::string(locality_addr.ptr, locality_addr.length));

  // Out of bounds host_index within a locality.
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_locality_host_address(dm_lb, 0, 1, 99,
                                                                                  &locality_addr));
  // Out of bounds priority.
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_locality_host_address(dm_lb, 99, 0, 0,
                                                                                  &locality_addr));

  // ---- Test locality weight (success path). ----
  EXPECT_EQ(50, envoy_dynamic_module_callback_cluster_lb_get_locality_weight(dm_lb, 0, 0));
  EXPECT_EQ(50, envoy_dynamic_module_callback_cluster_lb_get_locality_weight(dm_lb, 0, 1));
  // Out of bounds priority.
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_locality_weight(dm_lb, 99, 0));

  // ---- Test host locality (verify region/zone/sub_zone data). ----
  envoy_dynamic_module_type_envoy_buffer region = {}, zone = {}, sub_zone = {};
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_host_locality(dm_lb, 0, 0, &region,
                                                                         &zone, &sub_zone));
  EXPECT_EQ("us-east", std::string(region.ptr, region.length));
  EXPECT_EQ("us-east-1a", std::string(zone.ptr, zone.length));
  EXPECT_EQ("rack-1", std::string(sub_zone.ptr, sub_zone.length));
  // Out of bounds priority.
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_locality(dm_lb, 99, 0, &region,
                                                                          &zone, &sub_zone));

  // ---- Test host weight with out-of-bounds priority. ----
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_host_weight(dm_lb, 99, 0));

  // ---- Test healthy host weight with out-of-bounds priority. ----
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight(dm_lb, 99, 0));

  // ---- Test host address with out-of-bounds priority. ----
  envoy_dynamic_module_type_envoy_buffer addr_buf = {};
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_address(dm_lb, 99, 0, &addr_buf));

  // ---- Test host health with out-of-bounds priority. ----
  EXPECT_EQ(envoy_dynamic_module_type_host_health_Unhealthy,
            envoy_dynamic_module_callback_cluster_lb_get_host_health(dm_lb, 99, 0));

  // ---- Test host stat with out-of-bounds priority. ----
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_host_stat(
                   dm_lb, 99, 0, envoy_dynamic_module_type_host_stat_RqTotal));

  // ---- Test get_host_health_by_address using crossPriorityHostMap. ----
  // The MainPrioritySetImpl populates the cross-priority host map when updateHosts is called.
  envoy_dynamic_module_type_host_health health_result;
  envoy_dynamic_module_type_module_buffer host_addr = {"10.0.0.1:8080", 13};
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address(dm_lb, host_addr,
                                                                                  &health_result));
  EXPECT_EQ(envoy_dynamic_module_type_host_health_Healthy, health_result);

  // Test with an address that does not exist in the host map.
  envoy_dynamic_module_type_module_buffer unknown_addr = {"10.0.0.99:8080", 14};
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address(
      dm_lb, unknown_addr, &health_result));

  // ---- Test get_host_health with degraded and unhealthy hosts. ----
  auto degraded_host = Upstream::makeTestHost(cluster->info(), "tcp://10.0.0.4:8080", zone_a, 1, 0,
                                              envoy::config::core::v3::DEGRADED);
  auto unhealthy_host = Upstream::makeTestHost(cluster->info(), "tcp://10.0.0.5:8080", zone_b, 1, 0,
                                               envoy::config::core::v3::UNHEALTHY);

  // Add degraded and unhealthy hosts to priority 1.
  auto p1_hosts =
      std::make_shared<Upstream::HostVector>(Upstream::HostVector{degraded_host, unhealthy_host});
  auto p1_hosts_per_locality = std::make_shared<Upstream::HostsPerLocalityImpl>(
      Upstream::HostVector{degraded_host, unhealthy_host});
  cluster->prioritySet().updateHosts(
      1, Upstream::HostSetImpl::partitionHosts(p1_hosts, p1_hosts_per_locality), nullptr,
      {degraded_host, unhealthy_host}, {}, absl::nullopt, absl::nullopt);

  EXPECT_EQ(envoy_dynamic_module_type_host_health_Degraded,
            envoy_dynamic_module_callback_cluster_lb_get_host_health(dm_lb, 1, 0));
  EXPECT_EQ(envoy_dynamic_module_type_host_health_Unhealthy,
            envoy_dynamic_module_callback_cluster_lb_get_host_health(dm_lb, 1, 1));

  // Also verify get_host_health_by_address for degraded and unhealthy hosts.
  envoy_dynamic_module_type_module_buffer degraded_addr = {"10.0.0.4:8080", 13};
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address(
      dm_lb, degraded_addr, &health_result));
  EXPECT_EQ(envoy_dynamic_module_type_host_health_Degraded, health_result);

  envoy_dynamic_module_type_module_buffer unhealthy_addr = {"10.0.0.5:8080", 13};
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address(
      dm_lb, unhealthy_addr, &health_result));
  EXPECT_EQ(envoy_dynamic_module_type_host_health_Unhealthy, health_result);
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
  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001"}, {1}, hosts));

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

// Test that the scheduler can be created and deleted via ABI callbacks.
TEST_F(DynamicModuleClusterTest, SchedulerLifecycle) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Create a scheduler via the ABI callback.
  auto* scheduler_ptr = envoy_dynamic_module_callback_cluster_scheduler_new(cluster.get());
  EXPECT_NE(scheduler_ptr, nullptr);

  // Delete the scheduler via the ABI callback.
  envoy_dynamic_module_callback_cluster_scheduler_delete(scheduler_ptr);
}

// Test that the scheduler commit posts to the dispatcher.
TEST_F(DynamicModuleClusterTest, SchedulerCommit) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Create a scheduler via the ABI callback.
  auto* scheduler_ptr = envoy_dynamic_module_callback_cluster_scheduler_new(cluster.get());
  EXPECT_NE(scheduler_ptr, nullptr);

  // Capture the posted callback from commit.
  Event::PostCb captured_cb;
  bool first_captured = false;
  ON_CALL(server_context_.dispatcher_, post(_))
      .WillByDefault(testing::Invoke([&](Event::PostCb cb) {
        if (!first_captured) {
          captured_cb = std::move(cb);
          first_captured = true;
        }
      }));

  // Commit an event via the ABI callback.
  envoy_dynamic_module_callback_cluster_scheduler_commit(scheduler_ptr, 42);
  ASSERT_TRUE(first_captured);

  // Execute the callback to complete the flow.
  captured_cb();

  // Clean up the scheduler and destroy the result before ON_CALL locals go out of scope.
  // The result destruction triggers DynamicModuleClusterHandle::~DynamicModuleClusterHandle which
  // posts to the dispatcher, so the ON_CALL lambda's captured references must still be alive.
  envoy_dynamic_module_callback_cluster_scheduler_delete(scheduler_ptr);
  cluster.reset();
  result = absl::InternalError("cleanup");
}

// Test that onScheduled is called when the posted callback executes.
TEST_F(DynamicModuleClusterTest, OnScheduledCallback) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Create a scheduler via the ABI callback.
  auto* scheduler_ptr = envoy_dynamic_module_callback_cluster_scheduler_new(cluster.get());
  EXPECT_NE(scheduler_ptr, nullptr);

  // Capture the posted callback from commit.
  Event::PostCb captured_cb;
  bool first_captured = false;
  ON_CALL(server_context_.dispatcher_, post(_))
      .WillByDefault(testing::Invoke([&](Event::PostCb cb) {
        if (!first_captured) {
          captured_cb = std::move(cb);
          first_captured = true;
        }
      }));

  // Commit an event via the ABI callback.
  envoy_dynamic_module_callback_cluster_scheduler_commit(scheduler_ptr, 123);
  ASSERT_TRUE(first_captured);

  // Execute the captured callback to trigger onScheduled.
  captured_cb();

  // Clean up the scheduler and destroy the result before ON_CALL locals go out of scope.
  envoy_dynamic_module_callback_cluster_scheduler_delete(scheduler_ptr);
  cluster.reset();
  result = absl::InternalError("cleanup");
}

// Test that onScheduled handles the case when cluster is already destroyed.
TEST_F(DynamicModuleClusterTest, OnScheduledAfterClusterDestroyed) {
  Event::PostCb captured_cb;
  bool first_captured = false;

  {
    auto result = createCluster(makeYamlConfig("cluster_no_op"));
    ASSERT_TRUE(result.ok()) << result.status().message();

    auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
    ASSERT_NE(nullptr, cluster);

    // Create a scheduler via the ABI callback.
    auto* scheduler_ptr = envoy_dynamic_module_callback_cluster_scheduler_new(cluster.get());
    EXPECT_NE(scheduler_ptr, nullptr);

    // Capture the posted callback from commit.
    ON_CALL(server_context_.dispatcher_, post(_))
        .WillByDefault(testing::Invoke([&](Event::PostCb cb) {
          if (!first_captured) {
            captured_cb = std::move(cb);
            first_captured = true;
          }
        }));

    // Commit an event via the ABI callback.
    envoy_dynamic_module_callback_cluster_scheduler_commit(scheduler_ptr, 456);
    ASSERT_TRUE(first_captured);

    // Delete the scheduler before the callback is executed.
    envoy_dynamic_module_callback_cluster_scheduler_delete(scheduler_ptr);

    // Explicitly destroy the result while ON_CALL locals are still alive.
    // The destruction triggers DynamicModuleClusterHandle::~DynamicModuleClusterHandle which
    // posts to the dispatcher.
    cluster.reset();
    result = absl::InternalError("cleanup");
  }

  // Execute the captured callback after cluster is destroyed.
  // This should not crash - the weak_ptr should be expired.
  captured_cb();
}

// `commit` must not touch the dispatcher once the owning cluster has been destroyed.
TEST_F(DynamicModuleClusterTest, CommitAfterClusterDestroyedDoesNotTouchDispatcher) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  auto* scheduler_ptr = envoy_dynamic_module_callback_cluster_scheduler_new(cluster.get());
  EXPECT_NE(scheduler_ptr, nullptr);

  cluster.reset();
  result = absl::InternalError("cleanup");

  EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);

  envoy_dynamic_module_callback_cluster_scheduler_commit(scheduler_ptr, 42);

  envoy_dynamic_module_callback_cluster_scheduler_delete(scheduler_ptr);
}

// Test calling onScheduled directly.
TEST_F(DynamicModuleClusterTest, OnScheduledDirect) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Call onScheduled directly - this should call the in-module hook.
  cluster->onScheduled(789);
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

// Covers that a worker-thread handle destruction posts the full teardown to the main dispatcher.
TEST_F(DynamicModuleClusterTest, HandleDestructorFromWorkerThreadDefersAllTeardownToMainThread) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // The internal handle held by the thread-aware load balancer also posts on its main-thread
  // destruction when `result` is torn down, so allow repeated posts and capture the first one
  // from the worker-thread handle destruction.
  Event::PostCb captured_cb;
  EXPECT_CALL(server_context_.dispatcher_, post(_))
      .WillRepeatedly(testing::Invoke([&](Event::PostCb cb) {
        if (!captured_cb) {
          captured_cb = std::move(cb);
        }
      }));

  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
  auto& thread_factory = Thread::threadFactoryForTest();
  auto thread = thread_factory.createThread([&]() { handle.reset(); });
  thread->join();

  ASSERT_TRUE(captured_cb);
  EXPECT_NE(cluster.use_count(), 1);
  captured_cb();
  cluster.reset();
  result = absl::InternalError("cleanup");
}

// Test that the server_initialized lifecycle callback is invoked.
TEST_F(DynamicModuleClusterTest, ServerInitializedCallback) {
  // Capture the PostInit callback registered during cluster construction.
  Server::ServerLifecycleNotifier::StageCallback captured_cb;
  EXPECT_CALL(server_context_.lifecycle_notifier_,
              registerCallback(Server::ServerLifecycleNotifier::Stage::PostInit,
                               testing::An<Server::ServerLifecycleNotifier::StageCallback>()))
      .WillOnce(testing::DoAll(testing::SaveArg<1>(&captured_cb), testing::Return(nullptr)));

  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  // Invoke the captured callback to exercise the server initialized path.
  captured_cb();
}

// Test that the drain_started lifecycle callback is invoked.
TEST_F(DynamicModuleClusterTest, DrainStartedCallback) {
  // Capture the drain callback registered during cluster construction.
  Server::DrainManager::DrainCloseCb captured_drain_cb;
  EXPECT_CALL(server_context_.drain_manager_, addOnDrainCloseCb(Network::DrainDirection::All, _))
      .WillOnce(testing::DoAll(testing::SaveArg<1>(&captured_drain_cb), testing::Return(nullptr)));

  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  // Invoke the captured drain callback to exercise the drain notification path.
  EXPECT_TRUE(captured_drain_cb(std::chrono::milliseconds(0)).ok());
}

// Test that the shutdown lifecycle callback is invoked with completion.
TEST_F(DynamicModuleClusterTest, ShutdownCallbackWithCompletion) {
  // Capture the shutdown callback registered during cluster construction.
  Server::ServerLifecycleNotifier::StageCallbackWithCompletion captured_shutdown_cb;
  EXPECT_CALL(
      server_context_.lifecycle_notifier_,
      registerCallback(Server::ServerLifecycleNotifier::Stage::ShutdownExit,
                       testing::An<Server::ServerLifecycleNotifier::StageCallbackWithCompletion>()))
      .WillOnce(
          testing::DoAll(testing::SaveArg<1>(&captured_shutdown_cb), testing::Return(nullptr)));

  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  // Invoke the captured shutdown callback with a completion callback.
  bool completion_called = false;
  captured_shutdown_cb([&completion_called]() { completion_called = true; });
  EXPECT_TRUE(completion_called);
}

// Test that shutdown completion is still called when the in-module cluster is null.
TEST_F(DynamicModuleClusterTest, ShutdownCallbackAfterClusterDestroy) {
  // Capture the shutdown callback registered during cluster construction.
  Server::ServerLifecycleNotifier::StageCallbackWithCompletion captured_shutdown_cb;
  EXPECT_CALL(
      server_context_.lifecycle_notifier_,
      registerCallback(Server::ServerLifecycleNotifier::Stage::ShutdownExit,
                       testing::An<Server::ServerLifecycleNotifier::StageCallbackWithCompletion>()))
      .WillOnce(
          testing::DoAll(testing::SaveArg<1>(&captured_shutdown_cb), testing::Return(nullptr)));

  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  // Destroy the cluster to null out the in-module pointer.
  result->first.reset();

  // Invoke the captured shutdown callback. Since the cluster is destroyed, the completion
  // callback should still be invoked (the else branch).
  bool completion_called = false;
  captured_shutdown_cb([&completion_called]() { completion_called = true; });
  EXPECT_TRUE(completion_called);
}

// Test that all lifecycle callbacks are registered during cluster creation.
TEST_F(DynamicModuleClusterTest, AllLifecycleCallbacksRegistered) {
  // Verify that all three lifecycle callbacks are registered.
  Server::ServerLifecycleNotifier::StageCallback captured_init_cb;
  Server::DrainManager::DrainCloseCb captured_drain_cb;
  Server::ServerLifecycleNotifier::StageCallbackWithCompletion captured_shutdown_cb;

  EXPECT_CALL(server_context_.lifecycle_notifier_,
              registerCallback(Server::ServerLifecycleNotifier::Stage::PostInit,
                               testing::An<Server::ServerLifecycleNotifier::StageCallback>()))
      .WillOnce(testing::DoAll(testing::SaveArg<1>(&captured_init_cb), testing::Return(nullptr)));
  EXPECT_CALL(server_context_.drain_manager_, addOnDrainCloseCb(Network::DrainDirection::All, _))
      .WillOnce(testing::DoAll(testing::SaveArg<1>(&captured_drain_cb), testing::Return(nullptr)));
  EXPECT_CALL(
      server_context_.lifecycle_notifier_,
      registerCallback(Server::ServerLifecycleNotifier::Stage::ShutdownExit,
                       testing::An<Server::ServerLifecycleNotifier::StageCallbackWithCompletion>()))
      .WillOnce(
          testing::DoAll(testing::SaveArg<1>(&captured_shutdown_cb), testing::Return(nullptr)));

  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  // Invoke all lifecycle callbacks to verify the full lifecycle flow.
  captured_init_cb();
  EXPECT_TRUE(captured_drain_cb(std::chrono::milliseconds(0)).ok());
  bool completion_called = false;
  captured_shutdown_cb([&completion_called]() { completion_called = true; });
  EXPECT_TRUE(completion_called);
}

// =============================================================================
// Metrics Tests
// =============================================================================

// Test defining and incrementing a scalar counter via the ABI callbacks.
TEST_F(DynamicModuleClusterTest, MetricsDefineAndIncrementCounter) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* config = cluster->config().get();
  unfreezeStatCreation(*config);

  // Define a scalar counter.
  size_t counter_id = 0;
  envoy_dynamic_module_type_module_buffer name = {const_cast<char*>("test_counter"),
                                                  strlen("test_counter")};
  auto define_result = envoy_dynamic_module_callback_cluster_config_define_counter(
      config, name, nullptr, 0, &counter_id);
  EXPECT_EQ(define_result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_GT(counter_id, 0);

  // Increment the counter.
  auto inc_result = envoy_dynamic_module_callback_cluster_config_increment_counter(
      config, counter_id, nullptr, 0, 5);
  EXPECT_EQ(inc_result, envoy_dynamic_module_type_metrics_result_Success);
}

// Test defining and using a scalar gauge via the ABI callbacks.
TEST_F(DynamicModuleClusterTest, MetricsDefineAndUseGauge) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* config = cluster->config().get();
  unfreezeStatCreation(*config);

  // Define a scalar gauge.
  size_t gauge_id = 0;
  envoy_dynamic_module_type_module_buffer name = {const_cast<char*>("test_gauge"),
                                                  strlen("test_gauge")};
  auto define_result = envoy_dynamic_module_callback_cluster_config_define_gauge(
      config, name, nullptr, 0, &gauge_id);
  EXPECT_EQ(define_result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_GT(gauge_id, 0);

  // Set, increment, and decrement the gauge.
  EXPECT_EQ(
      envoy_dynamic_module_callback_cluster_config_set_gauge(config, gauge_id, nullptr, 0, 42),
      envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_increment_gauge(config, gauge_id, nullptr,
                                                                         0, 10),
            envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(
      envoy_dynamic_module_callback_cluster_config_decrement_gauge(config, gauge_id, nullptr, 0, 5),
      envoy_dynamic_module_type_metrics_result_Success);
}

// Test defining and recording a scalar histogram via the ABI callbacks.
TEST_F(DynamicModuleClusterTest, MetricsDefineAndRecordHistogram) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* config = cluster->config().get();
  unfreezeStatCreation(*config);

  // Define a scalar histogram.
  size_t histogram_id = 0;
  envoy_dynamic_module_type_module_buffer name = {const_cast<char*>("test_histogram"),
                                                  strlen("test_histogram")};
  auto define_result = envoy_dynamic_module_callback_cluster_config_define_histogram(
      config, name, nullptr, 0, &histogram_id);
  EXPECT_EQ(define_result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_GT(histogram_id, 0);

  // Record a value.
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_record_histogram_value(
                config, histogram_id, nullptr, 0, 100),
            envoy_dynamic_module_type_metrics_result_Success);
}

// Test metric not found errors for invalid IDs.
TEST_F(DynamicModuleClusterTest, MetricsNotFoundForInvalidId) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* config = cluster->config().get();
  unfreezeStatCreation(*config);

  // Increment a counter that was never defined.
  EXPECT_EQ(
      envoy_dynamic_module_callback_cluster_config_increment_counter(config, 999, nullptr, 0, 1),
      envoy_dynamic_module_type_metrics_result_MetricNotFound);

  // Set a gauge that was never defined.
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_set_gauge(config, 999, nullptr, 0, 1),
            envoy_dynamic_module_type_metrics_result_MetricNotFound);

  // Increment a gauge that was never defined.
  EXPECT_EQ(
      envoy_dynamic_module_callback_cluster_config_increment_gauge(config, 999, nullptr, 0, 1),
      envoy_dynamic_module_type_metrics_result_MetricNotFound);

  // Decrement a gauge that was never defined.
  EXPECT_EQ(
      envoy_dynamic_module_callback_cluster_config_decrement_gauge(config, 999, nullptr, 0, 1),
      envoy_dynamic_module_type_metrics_result_MetricNotFound);

  // Record a histogram that was never defined.
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_record_histogram_value(config, 999,
                                                                                nullptr, 0, 1),
            envoy_dynamic_module_type_metrics_result_MetricNotFound);
}

// Test defining and using a counter vec with labels.
TEST_F(DynamicModuleClusterTest, MetricsCounterVecWithLabels) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* config = cluster->config().get();
  unfreezeStatCreation(*config);

  // Define a counter vec with two labels.
  size_t counter_id = 0;
  envoy_dynamic_module_type_module_buffer name = {const_cast<char*>("request_count"),
                                                  strlen("request_count")};
  envoy_dynamic_module_type_module_buffer label_names[2] = {
      {const_cast<char*>("region"), strlen("region")},
      {const_cast<char*>("status"), strlen("status")},
  };
  auto define_result = envoy_dynamic_module_callback_cluster_config_define_counter(
      config, name, label_names, 2, &counter_id);
  EXPECT_EQ(define_result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_GT(counter_id, 0);

  // Increment with correct label count.
  envoy_dynamic_module_type_module_buffer label_values[2] = {
      {const_cast<char*>("us-east-1"), strlen("us-east-1")},
      {const_cast<char*>("200"), strlen("200")},
  };
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_increment_counter(config, counter_id,
                                                                           label_values, 2, 1),
            envoy_dynamic_module_type_metrics_result_Success);

  // Increment with wrong label count should fail.
  envoy_dynamic_module_type_module_buffer wrong_label_values[1] = {
      {const_cast<char*>("us-east-1"), strlen("us-east-1")},
  };
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_increment_counter(
                config, counter_id, wrong_label_values, 1, 1),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);

  // Increment with zero labels on a vec metric should fail.
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_increment_counter(config, counter_id,
                                                                           nullptr, 0, 1),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);
}

// Test defining and using a gauge vec with labels.
TEST_F(DynamicModuleClusterTest, MetricsGaugeVecWithLabels) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* config = cluster->config().get();
  unfreezeStatCreation(*config);

  // Define a gauge vec.
  size_t gauge_id = 0;
  envoy_dynamic_module_type_module_buffer name = {const_cast<char*>("active_connections"),
                                                  strlen("active_connections")};
  envoy_dynamic_module_type_module_buffer label_names[1] = {
      {const_cast<char*>("endpoint"), strlen("endpoint")},
  };
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_define_gauge(config, name, label_names, 1,
                                                                      &gauge_id),
            envoy_dynamic_module_type_metrics_result_Success);

  envoy_dynamic_module_type_module_buffer label_values[1] = {
      {const_cast<char*>("10.0.0.1:80"), strlen("10.0.0.1:80")},
  };

  // Set, increment, and decrement the gauge vec.
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_set_gauge(config, gauge_id, label_values,
                                                                   1, 100),
            envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_increment_gauge(config, gauge_id,
                                                                         label_values, 1, 10),
            envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_decrement_gauge(config, gauge_id,
                                                                         label_values, 1, 5),
            envoy_dynamic_module_type_metrics_result_Success);
}

// Test defining and using a histogram vec with labels.
TEST_F(DynamicModuleClusterTest, MetricsHistogramVecWithLabels) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* config = cluster->config().get();
  unfreezeStatCreation(*config);

  // Define a histogram vec.
  size_t histogram_id = 0;
  envoy_dynamic_module_type_module_buffer name = {const_cast<char*>("latency"), strlen("latency")};
  envoy_dynamic_module_type_module_buffer label_names[1] = {
      {const_cast<char*>("method"), strlen("method")},
  };
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_define_histogram(config, name, label_names,
                                                                          1, &histogram_id),
            envoy_dynamic_module_type_metrics_result_Success);

  envoy_dynamic_module_type_module_buffer label_values[1] = {
      {const_cast<char*>("GET"), strlen("GET")},
  };
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_record_histogram_value(
                config, histogram_id, label_values, 1, 42),
            envoy_dynamic_module_type_metrics_result_Success);

  // Wrong label count should fail.
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_record_histogram_value(
                config, histogram_id, nullptr, 0, 42),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);
}

// Test that using a vec metric ID with zero labels returns InvalidLabels.
TEST_F(DynamicModuleClusterTest, MetricsVecScalarIdConflictErrors) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* config = cluster->config().get();
  unfreezeStatCreation(*config);

  envoy_dynamic_module_type_module_buffer label_name = {const_cast<char*>("lbl"), strlen("lbl")};

  // Define a counter vec.
  size_t counter_vec_id = 0;
  envoy_dynamic_module_type_module_buffer counter_name = {const_cast<char*>("cv"), strlen("cv")};
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_define_counter(
                config, counter_name, &label_name, 1, &counter_vec_id),
            envoy_dynamic_module_type_metrics_result_Success);

  // Calling increment_counter with 0 labels on a vec ID returns InvalidLabels.
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_increment_counter(config, counter_vec_id,
                                                                           nullptr, 0, 1),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);

  // Define a gauge vec.
  size_t gauge_vec_id = 0;
  envoy_dynamic_module_type_module_buffer gauge_name = {const_cast<char*>("gv"), strlen("gv")};
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_define_gauge(
                config, gauge_name, &label_name, 1, &gauge_vec_id),
            envoy_dynamic_module_type_metrics_result_Success);

  // Calling set_gauge, increment_gauge, decrement_gauge with 0 labels on a vec ID returns
  // InvalidLabels.
  EXPECT_EQ(
      envoy_dynamic_module_callback_cluster_config_set_gauge(config, gauge_vec_id, nullptr, 0, 1),
      envoy_dynamic_module_type_metrics_result_InvalidLabels);
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_increment_gauge(config, gauge_vec_id,
                                                                         nullptr, 0, 1),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_decrement_gauge(config, gauge_vec_id,
                                                                         nullptr, 0, 1),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);

  // Define a histogram vec.
  size_t hist_vec_id = 0;
  envoy_dynamic_module_type_module_buffer hist_name = {const_cast<char*>("hv"), strlen("hv")};
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_define_histogram(
                config, hist_name, &label_name, 1, &hist_vec_id),
            envoy_dynamic_module_type_metrics_result_Success);

  // Calling record_histogram_value with 0 labels on a vec ID returns InvalidLabels.
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_record_histogram_value(config, hist_vec_id,
                                                                                nullptr, 0, 1),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);
}

// Test that providing wrong number of label values returns InvalidLabels.
TEST_F(DynamicModuleClusterTest, MetricsVecWrongLabelCount) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* config = cluster->config().get();
  unfreezeStatCreation(*config);

  // Define a gauge vec with one label.
  envoy_dynamic_module_type_module_buffer gauge_name = {const_cast<char*>("gwl"), strlen("gwl")};
  envoy_dynamic_module_type_module_buffer label_name = {const_cast<char*>("lbl"), strlen("lbl")};
  size_t gauge_vec_id = 0;
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_define_gauge(
                config, gauge_name, &label_name, 1, &gauge_vec_id),
            envoy_dynamic_module_type_metrics_result_Success);

  // Providing wrong number of label values (2 instead of 1).
  envoy_dynamic_module_type_module_buffer extra_vals[2] = {
      {const_cast<char*>("a"), strlen("a")},
      {const_cast<char*>("b"), strlen("b")},
  };
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_set_gauge(config, gauge_vec_id, extra_vals,
                                                                   2, 50),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_increment_gauge(config, gauge_vec_id,
                                                                         extra_vals, 2, 10),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_decrement_gauge(config, gauge_vec_id,
                                                                         extra_vals, 2, 5),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);

  // Define a histogram vec with one label.
  envoy_dynamic_module_type_module_buffer hist_name = {const_cast<char*>("hwl"), strlen("hwl")};
  size_t hist_vec_id = 0;
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_define_histogram(
                config, hist_name, &label_name, 1, &hist_vec_id),
            envoy_dynamic_module_type_metrics_result_Success);

  // Providing wrong number of label values (2 instead of 1).
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_record_histogram_value(config, hist_vec_id,
                                                                                extra_vals, 2, 42),
            envoy_dynamic_module_type_metrics_result_InvalidLabels);
}

// Test that using non-existent vec IDs with labels returns MetricNotFound.
TEST_F(DynamicModuleClusterTest, MetricsVecNotFoundWithLabels) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* config = cluster->config().get();
  unfreezeStatCreation(*config);

  envoy_dynamic_module_type_module_buffer label_val = {const_cast<char*>("val"), strlen("val")};

  // Using non-existent vec IDs with labels should return MetricNotFound.
  EXPECT_EQ(
      envoy_dynamic_module_callback_cluster_config_increment_counter(config, 999, &label_val, 1, 1),
      envoy_dynamic_module_type_metrics_result_MetricNotFound);
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_set_gauge(config, 999, &label_val, 1, 10),
            envoy_dynamic_module_type_metrics_result_MetricNotFound);
  EXPECT_EQ(
      envoy_dynamic_module_callback_cluster_config_increment_gauge(config, 999, &label_val, 1, 10),
      envoy_dynamic_module_type_metrics_result_MetricNotFound);
  EXPECT_EQ(
      envoy_dynamic_module_callback_cluster_config_decrement_gauge(config, 999, &label_val, 1, 5),
      envoy_dynamic_module_type_metrics_result_MetricNotFound);
  EXPECT_EQ(envoy_dynamic_module_callback_cluster_config_record_histogram_value(config, 999,
                                                                                &label_val, 1, 42),
            envoy_dynamic_module_type_metrics_result_MetricNotFound);
}

// =============================================================================
// Cluster LB Context ABI Callback Tests
// =============================================================================

// Test compute_hash_key with a valid hash.
TEST_F(DynamicModuleClusterTest, LbContextComputeHashKey) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, computeHashKey()).WillByDefault(Return(absl::optional<uint64_t>(12345)));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  uint64_t hash = 0;
  EXPECT_TRUE(
      envoy_dynamic_module_callback_cluster_lb_context_compute_hash_key(context_ptr, &hash));
  EXPECT_EQ(12345, hash);
}

// Test compute_hash_key when no hash is available.
TEST_F(DynamicModuleClusterTest, LbContextComputeHashKeyNoHash) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, computeHashKey()).WillByDefault(Return(absl::nullopt));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  uint64_t hash = 0;
  EXPECT_FALSE(
      envoy_dynamic_module_callback_cluster_lb_context_compute_hash_key(context_ptr, &hash));
}

// Test compute_hash_key with nullptr context.
TEST_F(DynamicModuleClusterTest, LbContextComputeHashKeyNullContext) {
  uint64_t hash = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_compute_hash_key(nullptr, &hash));
}

// Test compute_hash_key with nullptr output.
TEST_F(DynamicModuleClusterTest, LbContextComputeHashKeyNullOutput) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  EXPECT_FALSE(
      envoy_dynamic_module_callback_cluster_lb_context_compute_hash_key(context_ptr, nullptr));
}

// Test get_downstream_headers_size with headers.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamHeadersSize) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {"x-test", "value"}};
  ON_CALL(context, downstreamHeaders()).WillByDefault(Return(&headers));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  EXPECT_EQ(
      2, envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size(context_ptr));
}

// Test get_downstream_headers_size with no headers.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamHeadersSizeNoHeaders) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, downstreamHeaders()).WillByDefault(Return(nullptr));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  EXPECT_EQ(
      0, envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size(context_ptr));
}

// Test get_downstream_headers_size with nullptr context.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamHeadersSizeNullContext) {
  EXPECT_EQ(0,
            envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size(nullptr));
}

// Test get_downstream_headers retrieves all headers.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamHeaders) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {"x-test", "value"}};
  ON_CALL(context, downstreamHeaders()).WillByDefault(Return(&headers));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  size_t size =
      envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size(context_ptr);
  ASSERT_EQ(2, size);

  std::vector<envoy_dynamic_module_type_envoy_http_header> result(size);
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers(
      context_ptr, result.data()));

  EXPECT_EQ(":method", absl::string_view(result[0].key_ptr, result[0].key_length));
  EXPECT_EQ("GET", absl::string_view(result[0].value_ptr, result[0].value_length));
  EXPECT_EQ("x-test", absl::string_view(result[1].key_ptr, result[1].key_length));
  EXPECT_EQ("value", absl::string_view(result[1].value_ptr, result[1].value_length));
}

// Test get_downstream_headers with no headers available.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamHeadersNoHeaders) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, downstreamHeaders()).WillByDefault(Return(nullptr));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  envoy_dynamic_module_type_envoy_http_header result;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers(context_ptr,
                                                                                       &result));
}

// Test get_downstream_headers with nullptr context.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamHeadersNullContext) {
  envoy_dynamic_module_type_envoy_http_header result;
  EXPECT_FALSE(
      envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers(nullptr, &result));
}

// Test get_downstream_headers with nullptr result.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamHeadersNullResult) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers(context_ptr,
                                                                                       nullptr));
}

// Test get_downstream_header by key.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamHeader) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  Http::TestRequestHeaderMapImpl headers{{"x-custom", "val1"}};
  ON_CALL(context, downstreamHeaders()).WillByDefault(Return(&headers));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  std::string key = "x-custom";
  envoy_dynamic_module_type_module_buffer key_buf = {key.data(), key.size()};
  envoy_dynamic_module_type_envoy_buffer result_buf;
  size_t total_size = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header(
      context_ptr, key_buf, &result_buf, 0, &total_size));
  EXPECT_EQ("val1", absl::string_view(result_buf.ptr, result_buf.length));
  EXPECT_EQ(1, total_size);
}

// Test get_downstream_header with index out of bounds.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamHeaderOutOfBounds) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  Http::TestRequestHeaderMapImpl headers{{"x-custom", "val1"}};
  ON_CALL(context, downstreamHeaders()).WillByDefault(Return(&headers));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  std::string key = "x-custom";
  envoy_dynamic_module_type_module_buffer key_buf = {key.data(), key.size()};
  envoy_dynamic_module_type_envoy_buffer result_buf;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header(
      context_ptr, key_buf, &result_buf, 1, nullptr));
}

// Test get_downstream_header with nonexistent key.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamHeaderNotFound) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  Http::TestRequestHeaderMapImpl headers{{"x-custom", "val1"}};
  ON_CALL(context, downstreamHeaders()).WillByDefault(Return(&headers));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  std::string key = "x-missing";
  envoy_dynamic_module_type_module_buffer key_buf = {key.data(), key.size()};
  envoy_dynamic_module_type_envoy_buffer result_buf;
  size_t total_size = 999;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header(
      context_ptr, key_buf, &result_buf, 0, &total_size));
  EXPECT_EQ(0, total_size);
}

// Test get_downstream_header with nullptr context.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamHeaderNullContext) {
  std::string key = "x-custom";
  envoy_dynamic_module_type_module_buffer key_buf = {key.data(), key.size()};
  envoy_dynamic_module_type_envoy_buffer result_buf;
  size_t total_size = 999;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header(
      nullptr, key_buf, &result_buf, 0, &total_size));
  EXPECT_EQ(0, total_size);
  EXPECT_EQ(nullptr, result_buf.ptr);
}

// Test get_downstream_header with no headers.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamHeaderNoHeaders) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, downstreamHeaders()).WillByDefault(Return(nullptr));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  std::string key = "x-custom";
  envoy_dynamic_module_type_module_buffer key_buf = {key.data(), key.size()};
  envoy_dynamic_module_type_envoy_buffer result_buf;
  size_t total_size = 999;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header(
      context_ptr, key_buf, &result_buf, 0, &total_size));
  EXPECT_EQ(0, total_size);
}

// Test get_host_selection_retry_count.
TEST_F(DynamicModuleClusterTest, LbContextGetHostSelectionRetryCount) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, hostSelectionRetryCount()).WillByDefault(Return(3));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  EXPECT_EQ(3, envoy_dynamic_module_callback_cluster_lb_context_get_host_selection_retry_count(
                   context_ptr));
}

// Test get_host_selection_retry_count with nullptr context.
TEST_F(DynamicModuleClusterTest, LbContextGetHostSelectionRetryCountNullContext) {
  EXPECT_EQ(
      0, envoy_dynamic_module_callback_cluster_lb_context_get_host_selection_retry_count(nullptr));
}

// Test should_select_another_host.
TEST_F(DynamicModuleClusterTest, LbContextShouldSelectAnotherHost) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001", "127.0.0.1:10002"}, {1, 2}, hosts));

  auto& talb = result->second;
  EXPECT_TRUE(talb->initialize().ok());
  auto lb_factory = talb->factory();
  NiceMock<Upstream::MockPrioritySet> mock_ps;
  auto lb = lb_factory->create({mock_ps});
  auto* dm_lb = dynamic_cast<DynamicModuleLoadBalancer*>(lb.get());
  ASSERT_NE(nullptr, dm_lb);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, shouldSelectAnotherHost(_)).WillByDefault(Return(true));
  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host(
      dm_lb, context_ptr, 0, 0));
}

// Test should_select_another_host returns false.
TEST_F(DynamicModuleClusterTest, LbContextShouldSelectAnotherHostReturnsFalse) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001"}, {1}, hosts));

  auto& talb = result->second;
  EXPECT_TRUE(talb->initialize().ok());
  auto lb_factory = talb->factory();
  NiceMock<Upstream::MockPrioritySet> mock_ps;
  auto lb = lb_factory->create({mock_ps});
  auto* dm_lb = dynamic_cast<DynamicModuleLoadBalancer*>(lb.get());
  ASSERT_NE(nullptr, dm_lb);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, shouldSelectAnotherHost(_)).WillByDefault(Return(false));
  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host(
      dm_lb, context_ptr, 0, 0));
}

// Test should_select_another_host with invalid priority and index.
TEST_F(DynamicModuleClusterTest, LbContextShouldSelectAnotherHostInvalidArgs) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001"}, {1}, hosts));

  auto& talb = result->second;
  EXPECT_TRUE(talb->initialize().ok());
  auto lb_factory = talb->factory();
  NiceMock<Upstream::MockPrioritySet> mock_ps;
  auto lb = lb_factory->create({mock_ps});
  auto* dm_lb = dynamic_cast<DynamicModuleLoadBalancer*>(lb.get());
  ASSERT_NE(nullptr, dm_lb);

  NiceMock<Upstream::MockLoadBalancerContext> context;
  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  // Invalid priority.
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host(
      dm_lb, context_ptr, 99, 0));
  // Invalid index.
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host(
      dm_lb, context_ptr, 0, 99));
}

// Test should_select_another_host with nullptr context.
TEST_F(DynamicModuleClusterTest, LbContextShouldSelectAnotherHostNullContext) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto& talb = result->second;
  EXPECT_TRUE(talb->initialize().ok());
  auto lb_factory = talb->factory();
  NiceMock<Upstream::MockPrioritySet> mock_ps;
  auto lb = lb_factory->create({mock_ps});
  auto* dm_lb = dynamic_cast<DynamicModuleLoadBalancer*>(lb.get());
  ASSERT_NE(nullptr, dm_lb);

  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host(
      dm_lb, nullptr, 0, 0));
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host(
      nullptr, nullptr, 0, 0));
}

// Test get_override_host with an override set.
TEST_F(DynamicModuleClusterTest, LbContextGetOverrideHostPresent) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  Upstream::LoadBalancerContext::OverrideHost override_host{"10.0.0.1:8080", true};
  ON_CALL(context, overrideHostToSelect())
      .WillByDefault(
          Return(OptRef<const Upstream::LoadBalancerContext::OverrideHost>(override_host)));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  envoy_dynamic_module_type_envoy_buffer address;
  bool strict = false;
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_context_get_override_host(
      context_ptr, &address, &strict));
  EXPECT_EQ("10.0.0.1:8080", absl::string_view(address.ptr, address.length));
  EXPECT_TRUE(strict);
}

// Test get_override_host non-strict.
TEST_F(DynamicModuleClusterTest, LbContextGetOverrideHostNonStrict) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  Upstream::LoadBalancerContext::OverrideHost override_host{"10.0.0.2:9090", false};
  ON_CALL(context, overrideHostToSelect())
      .WillByDefault(
          Return(OptRef<const Upstream::LoadBalancerContext::OverrideHost>(override_host)));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  envoy_dynamic_module_type_envoy_buffer address;
  bool strict = true;
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_context_get_override_host(
      context_ptr, &address, &strict));
  EXPECT_EQ("10.0.0.2:9090", absl::string_view(address.ptr, address.length));
  EXPECT_FALSE(strict);
}

// Test get_override_host when not set.
TEST_F(DynamicModuleClusterTest, LbContextGetOverrideHostNotSet) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, overrideHostToSelect())
      .WillByDefault(Return(OptRef<const Upstream::LoadBalancerContext::OverrideHost>()));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  envoy_dynamic_module_type_envoy_buffer address;
  bool strict = false;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_get_override_host(
      context_ptr, &address, &strict));
}

// Test get_override_host with nullptr context.
TEST_F(DynamicModuleClusterTest, LbContextGetOverrideHostNullContext) {
  envoy_dynamic_module_type_envoy_buffer address;
  bool strict = false;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_get_override_host(nullptr, &address,
                                                                                  &strict));
}

// Test get_override_host with nullptr outputs.
TEST_F(DynamicModuleClusterTest, LbContextGetOverrideHostNullOutputs) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  // Null address.
  bool strict = false;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_get_override_host(
      context_ptr, nullptr, &strict));
  // Null strict.
  envoy_dynamic_module_type_envoy_buffer address;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_get_override_host(
      context_ptr, &address, nullptr));
}

// Test get_downstream_connection_sni with SNI available.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamConnectionSni) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<Network::MockConnection> connection;
  ON_CALL(context, downstreamConnection()).WillByDefault(Return(&connection));
  ON_CALL(connection, requestedServerName()).WillByDefault(Return("example.com"));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_context_get_downstream_connection_sni(
      context_ptr, &result));
  EXPECT_EQ("example.com", absl::string_view(result.ptr, result.length));
}

// Test get_downstream_connection_sni with empty SNI.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamConnectionSniEmpty) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  NiceMock<Network::MockConnection> connection;
  ON_CALL(context, downstreamConnection()).WillByDefault(Return(&connection));
  ON_CALL(connection, requestedServerName()).WillByDefault(Return(""));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_get_downstream_connection_sni(
      context_ptr, &result));
}

// Test get_downstream_connection_sni with no downstream connection.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamConnectionSniNoConnection) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, downstreamConnection()).WillByDefault(Return(nullptr));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_get_downstream_connection_sni(
      context_ptr, &result));
}

// Test get_downstream_connection_sni with nullptr context.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamConnectionSniNullContext) {
  envoy_dynamic_module_type_envoy_buffer result;
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_get_downstream_connection_sni(
      nullptr, &result));
}

// Test get_downstream_connection_sni with nullptr result buffer.
TEST_F(DynamicModuleClusterTest, LbContextGetDownstreamConnectionSniNullResult) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_context_get_downstream_connection_sni(
      context_ptr, nullptr));
}

// =================================================================================================
// Async Host Selection Tests
// =================================================================================================

// Test DynamicModuleAsyncHostSelectionHandle cancel calls the module's cancel function.
TEST_F(DynamicModuleClusterTest, AsyncHostSelectionHandleCancel) {
  auto* dummy_async_handle =
      reinterpret_cast<envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr>(0xCAFE);
  auto* dummy_lb = reinterpret_cast<envoy_dynamic_module_type_cluster_lb_module_ptr>(0xBEEF);

  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  DynamicModuleAsyncHostSelectionHandle handle(dummy_async_handle, dummy_lb, nullptr, cancelled);
  handle.cancel();
  EXPECT_TRUE(cancelled->load());
}

// Test DynamicModuleAsyncHostSelectionHandle cancel with null cancel_fn.
TEST_F(DynamicModuleClusterTest, AsyncHostSelectionHandleCancelNullFn) {
  auto* dummy_async_handle =
      reinterpret_cast<envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr>(0xCAFE);
  auto* dummy_lb = reinterpret_cast<envoy_dynamic_module_type_cluster_lb_module_ptr>(0xBEEF);

  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  DynamicModuleAsyncHostSelectionHandle handle(dummy_async_handle, dummy_lb, nullptr, cancelled);
  // Should not crash with nullptr cancel function.
  handle.cancel();
}

// Test async host selection complete callback with a valid host.
TEST_F(DynamicModuleClusterTest, AsyncHostSelectionCompleteWithHost) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok());
  auto& [cluster, lb] = result.value();

  auto& module_cluster = dynamic_cast<DynamicModuleCluster&>(*cluster);

  // Add a host so we can find it later.
  std::vector<Upstream::HostSharedPtr> result_hosts;
  ASSERT_TRUE(addSimpleHosts(module_cluster, {"127.0.0.1:8080"}, {1}, result_hosts));
  ASSERT_EQ(result_hosts.size(), 1);
  auto* raw_host_ptr = const_cast<Upstream::Host*>(result_hosts[0].get());

  // Create a mock LB context that expects onAsyncHostSelection.
  NiceMock<Upstream::MockLoadBalancerContext> context;
  EXPECT_CALL(context, onAsyncHostSelection(_, _))
      .WillOnce([&raw_host_ptr](Upstream::HostConstSharedPtr&& host, std::string&&) {
        EXPECT_EQ(host.get(), raw_host_ptr);
      });

  // Create a handle for the async completion callback.
  auto handle = std::make_shared<DynamicModuleClusterHandle>(
      std::dynamic_pointer_cast<DynamicModuleCluster>(cluster));
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());

  auto* lb_envoy_ptr = static_cast<void*>(lb_instance.get());
  auto* context_ptr = static_cast<void*>(&context);

  envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete(
      lb_envoy_ptr, context_ptr, raw_host_ptr, {"resolved", 8});
}

// Test async host selection complete callback with null host.
TEST_F(DynamicModuleClusterTest, AsyncHostSelectionCompleteNullHost) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok());
  auto& [cluster, lb] = result.value();

  NiceMock<Upstream::MockLoadBalancerContext> context;
  EXPECT_CALL(context, onAsyncHostSelection(_, _))
      .WillOnce([](Upstream::HostConstSharedPtr&& host, std::string&& details) {
        EXPECT_EQ(host, nullptr);
        EXPECT_EQ(details, "dns_failure");
      });

  auto handle = std::make_shared<DynamicModuleClusterHandle>(
      std::dynamic_pointer_cast<DynamicModuleCluster>(cluster));
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());

  auto* lb_envoy_ptr = static_cast<void*>(lb_instance.get());
  auto* context_ptr = static_cast<void*>(&context);

  envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete(
      lb_envoy_ptr, context_ptr, nullptr, {"dns_failure", 11});
}

// Test async host selection complete callback with empty details.
TEST_F(DynamicModuleClusterTest, AsyncHostSelectionCompleteEmptyDetails) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok());
  auto& [cluster, lb] = result.value();

  NiceMock<Upstream::MockLoadBalancerContext> context;
  EXPECT_CALL(context, onAsyncHostSelection(_, _))
      .WillOnce([](Upstream::HostConstSharedPtr&& host, std::string&& details) {
        EXPECT_EQ(host, nullptr);
        EXPECT_TRUE(details.empty());
      });

  auto handle = std::make_shared<DynamicModuleClusterHandle>(
      std::dynamic_pointer_cast<DynamicModuleCluster>(cluster));
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());

  auto* lb_envoy_ptr = static_cast<void*>(lb_instance.get());
  auto* context_ptr = static_cast<void*>(&context);

  envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete(lb_envoy_ptr, context_ptr,
                                                                         nullptr, {nullptr, 0});
}

// Covers that `async_host_selection_complete` drops the event when the owning load balancer has
// already been destroyed.
TEST_F(DynamicModuleClusterTest, AsyncHostSelectionCompleteAfterLbDestroyedDropsEvent) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok());
  auto& [cluster, lb] = result.value();

  auto handle = std::make_shared<DynamicModuleClusterHandle>(
      std::dynamic_pointer_cast<DynamicModuleCluster>(cluster));
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());
  void* raw_lb_ptr = lb_instance.get();

  lb_instance.reset();

  NiceMock<Upstream::MockLoadBalancerContext> context;
  EXPECT_CALL(context, onAsyncHostSelection(_, _)).Times(0);

  envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete(
      raw_lb_ptr, static_cast<void*>(&context), nullptr, {"dropped", 7});
}

// Covers pointer reuse: a new load balancer allocated at the same address as a freed one must
// still be found by the registry.
TEST_F(DynamicModuleClusterTest, AsyncHostSelectionCompleteRegistersFreshInstancePerLb) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok());
  auto& [cluster, lb] = result.value();

  auto handle = std::make_shared<DynamicModuleClusterHandle>(
      std::dynamic_pointer_cast<DynamicModuleCluster>(cluster));

  auto first = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());
  first.reset();
  auto second = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());

  bool found = DynamicModuleLoadBalancer::withActiveInstance(
      second.get(), [](const DynamicModuleLoadBalancer&) {});
  EXPECT_TRUE(found);
}

// =============================================================================
// HTTP Callout Tests
// =============================================================================

// Test HTTP callout with cluster not found.
TEST_F(DynamicModuleClusterTest, HttpCalloutClusterNotFound) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok());
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(cluster, nullptr);

  // Initialize and complete pre-init so cluster is ready.
  cluster->initialize([] { return absl::OkStatus(); });
  cluster->preInitComplete();

  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("nonexistent_cluster"))
      .WillOnce(testing::Return(nullptr));

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"nonexistent_cluster", 19};
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "GET", 3},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};
  auto callout_result = envoy_dynamic_module_callback_cluster_http_callout(
      cluster.get(), &callout_id, cluster_name, headers.data(), headers.size(), body, 5000);
  EXPECT_EQ(callout_result, envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound);
}

// Test HTTP callout with missing required headers.
TEST_F(DynamicModuleClusterTest, HttpCalloutMissingHeaders) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok());
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(cluster, nullptr);

  cluster->initialize([] { return absl::OkStatus(); });
  cluster->preInitComplete();

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  // Missing :method, :path, host.
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {"x-custom", 8, "value", 5},
  };
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};
  auto callout_result = envoy_dynamic_module_callback_cluster_http_callout(
      cluster.get(), &callout_id, cluster_name, headers.data(), headers.size(), body, 5000);
  EXPECT_EQ(callout_result,
            envoy_dynamic_module_type_http_callout_init_result_MissingRequiredHeaders);
}

// Test HTTP callout success with response headers and body.
TEST_F(DynamicModuleClusterTest, HttpCalloutSuccess) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok());
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(cluster, nullptr);

  cluster->initialize([] { return absl::OkStatus(); });
  cluster->preInitComplete();

  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&thread_local_cluster));

  Http::MockAsyncClientRequest request(&thread_local_cluster.async_client_);
  Http::AsyncClient::Callbacks* callbacks_captured = nullptr;
  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_captured = &callbacks;
            return &request;
          }));

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "GET", 3},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};
  auto callout_result = envoy_dynamic_module_callback_cluster_http_callout(
      cluster.get(), &callout_id, cluster_name, headers.data(), headers.size(), body, 5000);
  EXPECT_EQ(callout_result, envoy_dynamic_module_type_http_callout_init_result_Success);
  EXPECT_NE(callout_id, 0);
  ASSERT_NE(callbacks_captured, nullptr);

  // Simulate a successful response.
  Http::ResponseHeaderMapPtr resp_headers(new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(std::move(resp_headers)));
  response->body().add("response_body");
  callbacks_captured->onSuccess(request, std::move(response));
}

// Test HTTP callout failure with reset.
TEST_F(DynamicModuleClusterTest, HttpCalloutFailureReset) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok());
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(cluster, nullptr);

  cluster->initialize([] { return absl::OkStatus(); });
  cluster->preInitComplete();

  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&thread_local_cluster));

  Http::MockAsyncClientRequest request(&thread_local_cluster.async_client_);
  Http::AsyncClient::Callbacks* callbacks_captured = nullptr;
  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_captured = &callbacks;
            return &request;
          }));

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "GET", 3},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};
  auto callout_result = envoy_dynamic_module_callback_cluster_http_callout(
      cluster.get(), &callout_id, cluster_name, headers.data(), headers.size(), body, 5000);
  EXPECT_EQ(callout_result, envoy_dynamic_module_type_http_callout_init_result_Success);
  ASSERT_NE(callbacks_captured, nullptr);

  callbacks_captured->onFailure(request, Http::AsyncClient::FailureReason::Reset);
}

// Test HTTP callout failure with exceed response buffer limit.
TEST_F(DynamicModuleClusterTest, HttpCalloutFailureExceedBufferLimit) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok());
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(cluster, nullptr);

  cluster->initialize([] { return absl::OkStatus(); });
  cluster->preInitComplete();

  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&thread_local_cluster));

  Http::MockAsyncClientRequest request(&thread_local_cluster.async_client_);
  Http::AsyncClient::Callbacks* callbacks_captured = nullptr;
  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_captured = &callbacks;
            return &request;
          }));

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "GET", 3},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};
  auto callout_result = envoy_dynamic_module_callback_cluster_http_callout(
      cluster.get(), &callout_id, cluster_name, headers.data(), headers.size(), body, 5000);
  EXPECT_EQ(callout_result, envoy_dynamic_module_type_http_callout_init_result_Success);
  ASSERT_NE(callbacks_captured, nullptr);

  callbacks_captured->onFailure(request,
                                Http::AsyncClient::FailureReason::ExceedResponseBufferLimit);
}

// Test HTTP callout when async client cannot create request.
TEST_F(DynamicModuleClusterTest, HttpCalloutCannotCreateRequest) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok());
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(cluster, nullptr);

  cluster->initialize([] { return absl::OkStatus(); });
  cluster->preInitComplete();

  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&thread_local_cluster));

  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Return(nullptr));

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "GET", 3},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};
  auto callout_result = envoy_dynamic_module_callback_cluster_http_callout(
      cluster.get(), &callout_id, cluster_name, headers.data(), headers.size(), body, 5000);
  EXPECT_EQ(callout_result, envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest);
}

// Test HTTP callout with request body.
TEST_F(DynamicModuleClusterTest, HttpCalloutWithBody) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok());
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(cluster, nullptr);

  cluster->initialize([] { return absl::OkStatus(); });
  cluster->preInitComplete();

  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&thread_local_cluster));

  Http::MockAsyncClientRequest request(&thread_local_cluster.async_client_);
  Http::AsyncClient::Callbacks* callbacks_captured = nullptr;
  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            // Verify the body was attached.
            EXPECT_EQ(message->body().toString(), "request_body");
            callbacks_captured = &callbacks;
            return &request;
          }));

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "POST", 4},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };
  envoy_dynamic_module_type_module_buffer body = {"request_body", 12};
  auto callout_result = envoy_dynamic_module_callback_cluster_http_callout(
      cluster.get(), &callout_id, cluster_name, headers.data(), headers.size(), body, 5000);
  EXPECT_EQ(callout_result, envoy_dynamic_module_type_http_callout_init_result_Success);
  ASSERT_NE(callbacks_captured, nullptr);

  // Simulate a successful response.
  Http::ResponseHeaderMapPtr resp_headers(new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(std::move(resp_headers)));
  callbacks_captured->onSuccess(request, std::move(response));
}

// Test HTTP callout success after in-module cluster is cleared.
TEST_F(DynamicModuleClusterTest, HttpCalloutSuccessAfterInModuleClusterCleared) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok());
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(cluster, nullptr);

  cluster->initialize([] { return absl::OkStatus(); });
  cluster->preInitComplete();

  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&thread_local_cluster));

  Http::MockAsyncClientRequest request(&thread_local_cluster.async_client_);
  Http::AsyncClient::Callbacks* callbacks_captured = nullptr;
  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_captured = &callbacks;
            return &request;
          }));

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "GET", 3},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};
  auto callout_result = envoy_dynamic_module_callback_cluster_http_callout(
      cluster.get(), &callout_id, cluster_name, headers.data(), headers.size(), body, 5000);
  EXPECT_EQ(callout_result, envoy_dynamic_module_type_http_callout_init_result_Success);
  ASSERT_NE(callbacks_captured, nullptr);

  // Clear the in-module cluster pointer to simulate the cluster being destroyed.
  DynamicModuleClusterTestPeer::clearInModuleCluster(*cluster);

  // The callback should not invoke on_cluster_http_callout_done and should just clean up.
  Http::ResponseHeaderMapPtr resp_headers(new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(std::move(resp_headers)));
  callbacks_captured->onSuccess(request, std::move(response));
}

// Test HTTP callout failure after in-module cluster is cleared.
TEST_F(DynamicModuleClusterTest, HttpCalloutFailureAfterInModuleClusterCleared) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok());
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(cluster, nullptr);

  cluster->initialize([] { return absl::OkStatus(); });
  cluster->preInitComplete();

  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  EXPECT_CALL(server_context_.cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&thread_local_cluster));

  Http::MockAsyncClientRequest request(&thread_local_cluster.async_client_);
  Http::AsyncClient::Callbacks* callbacks_captured = nullptr;
  EXPECT_CALL(thread_local_cluster.async_client_, send_(_, _, _))
      .WillOnce(testing::Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks_captured = &callbacks;
            return &request;
          }));

  uint64_t callout_id = 0;
  envoy_dynamic_module_type_module_buffer cluster_name = {"test_cluster", 12};
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {":method", 7, "GET", 3},
      {":path", 5, "/test", 5},
      {"host", 4, "example.com", 11},
  };
  envoy_dynamic_module_type_module_buffer body = {nullptr, 0};
  auto callout_result = envoy_dynamic_module_callback_cluster_http_callout(
      cluster.get(), &callout_id, cluster_name, headers.data(), headers.size(), body, 5000);
  EXPECT_EQ(callout_result, envoy_dynamic_module_type_http_callout_init_result_Success);
  ASSERT_NE(callbacks_captured, nullptr);

  // Clear the in-module cluster pointer to simulate the cluster being destroyed.
  DynamicModuleClusterTestPeer::clearInModuleCluster(*cluster);

  // The callback should not invoke on_cluster_http_callout_done and should just clean up.
  callbacks_captured->onFailure(request, Http::AsyncClient::FailureReason::Reset);
}

// Test adding hosts with locality information.
TEST_F(DynamicModuleClusterTest, AddHostsWithLocality) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  std::vector<std::string> addresses = {"127.0.0.1:10001", "127.0.0.1:10002"};
  std::vector<uint32_t> weights = {1, 2};
  std::vector<std::string> regions = {"us-east-1", "us-west-2"};
  std::vector<std::string> zones = {"us-east-1a", "us-west-2b"};
  std::vector<std::string> sub_zones = {"sub1", "sub2"};
  std::vector<std::vector<std::tuple<std::string, std::string, std::string>>> metadata;

  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(cluster->addHosts(addresses, weights, regions, zones, sub_zones, metadata, hosts));
  EXPECT_EQ(2, hosts.size());
  EXPECT_EQ(2, DynamicModuleClusterTestPeer::getHostMapSize(*cluster));

  // Verify localities are set correctly.
  EXPECT_EQ("us-east-1", hosts[0]->locality().region());
  EXPECT_EQ("us-east-1a", hosts[0]->locality().zone());
  EXPECT_EQ("sub1", hosts[0]->locality().sub_zone());
  EXPECT_EQ("us-west-2", hosts[1]->locality().region());
  EXPECT_EQ("us-west-2b", hosts[1]->locality().zone());
  EXPECT_EQ("sub2", hosts[1]->locality().sub_zone());

  // Verify hosts are in the priority set.
  EXPECT_EQ(2, cluster->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

// Test adding hosts with locality and metadata.
TEST_F(DynamicModuleClusterTest, AddHostsWithLocalityAndMetadata) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  std::vector<std::string> addresses = {"127.0.0.1:10001"};
  std::vector<uint32_t> weights = {1};
  std::vector<std::string> regions = {"us-east-1"};
  std::vector<std::string> zones = {"us-east-1a"};
  std::vector<std::string> sub_zones = {""};
  std::vector<std::vector<std::tuple<std::string, std::string, std::string>>> metadata = {
      {{"envoy.lb", "shard", "42"}, {"envoy.lb", "service", "my-service"}}};

  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(cluster->addHosts(addresses, weights, regions, zones, sub_zones, metadata, hosts));
  EXPECT_EQ(1, hosts.size());

  // Verify metadata is set correctly.
  EXPECT_NE(nullptr, hosts[0]->metadata());
  const auto& filter_metadata = hosts[0]->metadata()->filter_metadata();
  auto it = filter_metadata.find("envoy.lb");
  ASSERT_NE(it, filter_metadata.end());
  EXPECT_EQ("42", it->second.fields().at("shard").string_value());
  EXPECT_EQ("my-service", it->second.fields().at("service").string_value());
}

// Test adding hosts with locality via the ABI callback.
TEST_F(DynamicModuleClusterTest, AddHostsWithLocalityABI) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  auto* cluster_ptr = static_cast<void*>(cluster.get());

  // Build the ABI parameters.
  envoy_dynamic_module_type_module_buffer addr1 = {"127.0.0.1:10001", 15};
  envoy_dynamic_module_type_module_buffer addr2 = {"127.0.0.1:10002", 15};
  envoy_dynamic_module_type_module_buffer addrs[] = {addr1, addr2};
  uint32_t weights[] = {1, 2};

  envoy_dynamic_module_type_module_buffer region1 = {"us-east-1", 9};
  envoy_dynamic_module_type_module_buffer region2 = {"us-west-2", 9};
  envoy_dynamic_module_type_module_buffer regions[] = {region1, region2};

  envoy_dynamic_module_type_module_buffer zone1 = {"zone-a", 6};
  envoy_dynamic_module_type_module_buffer zone2 = {"zone-b", 6};
  envoy_dynamic_module_type_module_buffer zones[] = {zone1, zone2};

  envoy_dynamic_module_type_module_buffer sub1 = {"", 0};
  envoy_dynamic_module_type_module_buffer sub2 = {"", 0};
  envoy_dynamic_module_type_module_buffer sub_zones[] = {sub1, sub2};

  envoy_dynamic_module_type_cluster_host_envoy_ptr result_ptrs[2] = {nullptr, nullptr};

  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_add_hosts(
      cluster_ptr, 0, addrs, weights, regions, zones, sub_zones, nullptr, 0, 2, result_ptrs));

  EXPECT_NE(nullptr, result_ptrs[0]);
  EXPECT_NE(nullptr, result_ptrs[1]);
}

// Test adding hosts with locality and metadata via the ABI callback.
TEST_F(DynamicModuleClusterTest, AddHostsWithLocalityAndMetadataABI) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* cluster_ptr = static_cast<void*>(cluster.get());

  envoy_dynamic_module_type_module_buffer addr1 = {"127.0.0.1:10001", 15};
  envoy_dynamic_module_type_module_buffer addrs[] = {addr1};
  uint32_t weights[] = {1};
  envoy_dynamic_module_type_module_buffer regions[] = {{"us-east-1", 9}};
  envoy_dynamic_module_type_module_buffer zones[] = {{"zone-a", 6}};
  envoy_dynamic_module_type_module_buffer sub_zones[] = {{"", 0}};

  // One metadata triple per host: (filter_name, key, value).
  envoy_dynamic_module_type_module_buffer metadata[] = {{"envoy.lb", 8}, {"shard", 5}, {"42", 2}};

  envoy_dynamic_module_type_cluster_host_envoy_ptr result_ptrs[1] = {nullptr};

  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_add_hosts(
      cluster_ptr, 0, addrs, weights, regions, zones, sub_zones, metadata, 1, 1, result_ptrs));

  EXPECT_NE(nullptr, result_ptrs[0]);

  // Verify metadata through the LB host metadata ABI callbacks.
  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());
  auto* lb_ptr = static_cast<void*>(lb_instance.get());

  envoy_dynamic_module_type_envoy_buffer meta_result = {nullptr, 0};
  envoy_dynamic_module_type_module_buffer filter_name = {"envoy.lb", 8};
  envoy_dynamic_module_type_module_buffer key = {"shard", 5};
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string(
      lb_ptr, 0, 0, filter_name, key, &meta_result));
  EXPECT_EQ(2, meta_result.length);
  EXPECT_EQ("42", std::string(meta_result.ptr, meta_result.length));
}

// Test updating host health status.
TEST_F(DynamicModuleClusterTest, UpdateHostHealth) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);

  // Add a host first.
  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001"}, {1}, hosts));
  EXPECT_EQ(1, hosts.size());

  // Host should initially be healthy (UNKNOWN = healthy).
  EXPECT_EQ(Upstream::Host::Health::Healthy, hosts[0]->coarseHealth());

  // Mark as unhealthy.
  EXPECT_TRUE(cluster->updateHostHealth(hosts[0], envoy_dynamic_module_type_host_health_Unhealthy));
  EXPECT_EQ(Upstream::Host::Health::Unhealthy, hosts[0]->coarseHealth());

  // Mark as degraded.
  EXPECT_TRUE(cluster->updateHostHealth(hosts[0], envoy_dynamic_module_type_host_health_Degraded));
  EXPECT_EQ(Upstream::Host::Health::Degraded, hosts[0]->coarseHealth());

  // Mark as healthy again.
  EXPECT_TRUE(cluster->updateHostHealth(hosts[0], envoy_dynamic_module_type_host_health_Healthy));
  EXPECT_EQ(Upstream::Host::Health::Healthy, hosts[0]->coarseHealth());

  // Null host returns false.
  EXPECT_FALSE(cluster->updateHostHealth(nullptr, envoy_dynamic_module_type_host_health_Unhealthy));
}

// Test update_host_health via the ABI callback.
TEST_F(DynamicModuleClusterTest, UpdateHostHealthABI) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* cluster_ptr = static_cast<void*>(cluster.get());

  // Add a host via ABI.
  envoy_dynamic_module_type_module_buffer addr = {"127.0.0.1:10001", 15};
  uint32_t weight = 1;
  envoy_dynamic_module_type_module_buffer empty = {"", 0};
  envoy_dynamic_module_type_cluster_host_envoy_ptr host_ptr = nullptr;
  ASSERT_TRUE(envoy_dynamic_module_callback_cluster_add_hosts(
      cluster_ptr, 0, &addr, &weight, &empty, &empty, &empty, nullptr, 0, 1, &host_ptr));

  // Update to unhealthy via ABI.
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_update_host_health(
      cluster_ptr, host_ptr, envoy_dynamic_module_type_host_health_Unhealthy));

  // Verify via the LB health callback.
  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());
  auto* lb_ptr = static_cast<void*>(lb_instance.get());

  EXPECT_EQ(envoy_dynamic_module_type_host_health_Unhealthy,
            envoy_dynamic_module_callback_cluster_lb_get_host_health(lb_ptr, 0, 0));

  // Invalid host pointer returns false.
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_update_host_health(
      cluster_ptr, nullptr, envoy_dynamic_module_type_host_health_Healthy));
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_update_host_health(
      cluster_ptr, reinterpret_cast<void*>(0xDEAD), envoy_dynamic_module_type_host_health_Healthy));
}

// Test finding a host by address.
TEST_F(DynamicModuleClusterTest, FindHostByAddress) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);

  // Add hosts.
  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001", "127.0.0.1:10002"}, {1, 2}, hosts));

  // Find existing host by address.
  auto found = cluster->findHostByAddress("127.0.0.1:10001");
  EXPECT_NE(nullptr, found);
  EXPECT_EQ(hosts[0], found);

  found = cluster->findHostByAddress("127.0.0.1:10002");
  EXPECT_NE(nullptr, found);
  EXPECT_EQ(hosts[1], found);

  // Non-existent address returns nullptr.
  EXPECT_EQ(nullptr, cluster->findHostByAddress("127.0.0.1:99999"));
  EXPECT_EQ(nullptr, cluster->findHostByAddress(""));
}

// Test find_host_by_address via the ABI callback.
TEST_F(DynamicModuleClusterTest, FindHostByAddressABI) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* cluster_ptr = static_cast<void*>(cluster.get());

  // Add a host via ABI.
  envoy_dynamic_module_type_module_buffer addr = {"127.0.0.1:10001", 15};
  uint32_t weight = 1;
  envoy_dynamic_module_type_module_buffer empty = {"", 0};
  envoy_dynamic_module_type_cluster_host_envoy_ptr host_ptr = nullptr;
  ASSERT_TRUE(envoy_dynamic_module_callback_cluster_add_hosts(
      cluster_ptr, 0, &addr, &weight, &empty, &empty, &empty, nullptr, 0, 1, &host_ptr));

  // Find by address via ABI.
  envoy_dynamic_module_type_module_buffer search_addr = {"127.0.0.1:10001", 15};
  auto found = envoy_dynamic_module_callback_cluster_find_host_by_address(cluster_ptr, search_addr);
  EXPECT_EQ(host_ptr, found);

  // Non-existent address returns nullptr.
  envoy_dynamic_module_type_module_buffer bad_addr = {"127.0.0.1:99999", 15};
  EXPECT_EQ(nullptr,
            envoy_dynamic_module_callback_cluster_find_host_by_address(cluster_ptr, bad_addr));
}

// Test that the cluster LB on_host_membership_update callback fires and provides host addresses.
TEST_F(DynamicModuleClusterTest, LbHostMembershipUpdate) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);

  // Create an LB. The cluster_no_op module implements on_cluster_lb_on_host_membership_update,
  // so the LB should register for membership updates.
  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());

  // Add hosts - this should trigger the membership update callback on the LB.
  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001", "127.0.0.1:10002"}, {1, 2}, hosts));

  // Verify the LB can now see the hosts.
  auto* lb_ptr = static_cast<void*>(lb_instance.get());
  EXPECT_EQ(2, envoy_dynamic_module_callback_cluster_lb_get_hosts_count(lb_ptr, 0));

  // Remove a host - should also trigger the membership update callback.
  EXPECT_EQ(1, cluster->removeHosts({hosts[0]}));
  EXPECT_EQ(1, envoy_dynamic_module_callback_cluster_lb_get_hosts_count(lb_ptr, 0));
}

// Test get_member_update_host_address callback returns correct addresses during update.
TEST_F(DynamicModuleClusterTest, LbMemberUpdateHostAddress) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());
  auto* lb_ptr = static_cast<void*>(lb_instance.get());

  // When not in a membership update callback, the function should return false.
  envoy_dynamic_module_type_envoy_buffer addr_result = {nullptr, 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_member_update_host_address(
      lb_ptr, 0, true, &addr_result));
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_member_update_host_address(
      lb_ptr, 0, false, &addr_result));

  // Null parameters.
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_member_update_host_address(
      nullptr, 0, true, &addr_result));
  EXPECT_FALSE(envoy_dynamic_module_callback_cluster_lb_get_member_update_host_address(
      lb_ptr, 0, true, nullptr));
}

// Test hosts-per-locality is correctly maintained with locality-aware hosts.
TEST_F(DynamicModuleClusterTest, HostsPerLocalityWithLocality) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);

  // Add hosts with two different localities.
  std::vector<std::string> addresses = {"127.0.0.1:10001", "127.0.0.1:10002", "127.0.0.1:10003"};
  std::vector<uint32_t> weights = {1, 1, 1};
  std::vector<std::string> regions = {"us-east-1", "us-east-1", "us-west-2"};
  std::vector<std::string> zones = {"zone-a", "zone-a", "zone-b"};
  std::vector<std::string> sub_zones = {"", "", ""};
  std::vector<std::vector<std::tuple<std::string, std::string, std::string>>> metadata;

  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(cluster->addHosts(addresses, weights, regions, zones, sub_zones, metadata, hosts));

  // Verify through the LB that locality grouping works.
  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());
  auto* lb_ptr = static_cast<void*>(lb_instance.get());

  // Should have 2 locality buckets (the healthy hosts per locality).
  size_t locality_count = envoy_dynamic_module_callback_cluster_lb_get_locality_count(lb_ptr, 0);
  EXPECT_EQ(2, locality_count);

  // Verify locality info via host locality callback.
  envoy_dynamic_module_type_envoy_buffer region = {}, zone = {}, sub_zone = {};
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_host_locality(lb_ptr, 0, 0, &region,
                                                                         &zone, &sub_zone));
  EXPECT_EQ("us-east-1", std::string(region.ptr, region.length));
}

// Test that update_host_health affects the healthy host list.
TEST_F(DynamicModuleClusterTest, UpdateHostHealthAffectsHealthyHosts) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);

  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001", "127.0.0.1:10002"}, {1, 1}, hosts));

  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());
  auto* lb_ptr = static_cast<void*>(lb_instance.get());

  // Both hosts should be healthy initially.
  EXPECT_EQ(2, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(lb_ptr, 0));

  // Mark one as unhealthy.
  EXPECT_TRUE(cluster->updateHostHealth(hosts[0], envoy_dynamic_module_type_host_health_Unhealthy));

  // Now only one healthy host.
  EXPECT_EQ(1, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(lb_ptr, 0));

  // Restore health.
  EXPECT_TRUE(cluster->updateHostHealth(hosts[0], envoy_dynamic_module_type_host_health_Healthy));
  EXPECT_EQ(2, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(lb_ptr, 0));
}

// Test find_host_by_address on the cluster LB (worker thread).
TEST_F(DynamicModuleClusterTest, LbFindHostByAddress) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);

  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001", "127.0.0.1:10002"}, {1, 2}, hosts));

  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());
  auto* lb_ptr = static_cast<void*>(lb_instance.get());

  // Find existing host by address via the LB callback.
  envoy_dynamic_module_type_module_buffer addr1 = {"127.0.0.1:10001", 15};
  auto found = envoy_dynamic_module_callback_cluster_lb_find_host_by_address(lb_ptr, addr1);
  EXPECT_NE(nullptr, found);
  EXPECT_EQ(hosts[0].get(), found);

  envoy_dynamic_module_type_module_buffer addr2 = {"127.0.0.1:10002", 15};
  found = envoy_dynamic_module_callback_cluster_lb_find_host_by_address(lb_ptr, addr2);
  EXPECT_NE(nullptr, found);
  EXPECT_EQ(hosts[1].get(), found);

  // Non-existent address returns nullptr.
  envoy_dynamic_module_type_module_buffer bad_addr = {"127.0.0.1:99999", 15};
  EXPECT_EQ(nullptr,
            envoy_dynamic_module_callback_cluster_lb_find_host_by_address(lb_ptr, bad_addr));

  // Null parameters.
  EXPECT_EQ(nullptr, envoy_dynamic_module_callback_cluster_lb_find_host_by_address(nullptr, addr1));
  envoy_dynamic_module_type_module_buffer null_addr = {nullptr, 0};
  EXPECT_EQ(nullptr,
            envoy_dynamic_module_callback_cluster_lb_find_host_by_address(lb_ptr, null_addr));
}

// Test get_host returns a host pointer by index from all hosts regardless of health.
TEST_F(DynamicModuleClusterTest, LbGetHost) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);

  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001", "127.0.0.1:10002"}, {1, 2}, hosts));

  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());
  auto* lb_ptr = static_cast<void*>(lb_instance.get());

  // Get host by index from all hosts.
  auto host0 = envoy_dynamic_module_callback_cluster_lb_get_host(lb_ptr, 0, 0);
  EXPECT_NE(nullptr, host0);
  EXPECT_EQ(hosts[0].get(), host0);

  auto host1 = envoy_dynamic_module_callback_cluster_lb_get_host(lb_ptr, 0, 1);
  EXPECT_NE(nullptr, host1);
  EXPECT_EQ(hosts[1].get(), host1);

  // Out of bounds returns nullptr.
  EXPECT_EQ(nullptr, envoy_dynamic_module_callback_cluster_lb_get_host(lb_ptr, 0, 2));

  // Invalid priority returns nullptr.
  EXPECT_EQ(nullptr, envoy_dynamic_module_callback_cluster_lb_get_host(lb_ptr, 99, 0));

  // Null LB pointer returns nullptr.
  EXPECT_EQ(nullptr, envoy_dynamic_module_callback_cluster_lb_get_host(nullptr, 0, 0));
}

// Test get_host returns unhealthy hosts that get_healthy_host does not.
TEST_F(DynamicModuleClusterTest, LbGetHostIncludesUnhealthy) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);

  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001", "127.0.0.1:10002"}, {1, 1}, hosts));

  // Mark first host as unhealthy.
  EXPECT_TRUE(cluster->updateHostHealth(hosts[0], envoy_dynamic_module_type_host_health_Unhealthy));

  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());
  auto* lb_ptr = static_cast<void*>(lb_instance.get());

  // get_host should still return both hosts.
  EXPECT_EQ(2, envoy_dynamic_module_callback_cluster_lb_get_hosts_count(lb_ptr, 0));
  EXPECT_NE(nullptr, envoy_dynamic_module_callback_cluster_lb_get_host(lb_ptr, 0, 0));
  EXPECT_NE(nullptr, envoy_dynamic_module_callback_cluster_lb_get_host(lb_ptr, 0, 1));

  // get_healthy_host should only return one host.
  EXPECT_EQ(1, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(lb_ptr, 0));
  EXPECT_NE(nullptr, envoy_dynamic_module_callback_cluster_lb_get_healthy_host(lb_ptr, 0, 0));
  EXPECT_EQ(nullptr, envoy_dynamic_module_callback_cluster_lb_get_healthy_host(lb_ptr, 0, 1));
}

// Test that add_hosts supports adding hosts at a specific priority level.
TEST_F(DynamicModuleClusterTest, AddHostsToPriority) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);

  // Add hosts at priority 0.
  std::vector<Upstream::HostSharedPtr> hosts_p0;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001"}, {1}, hosts_p0, 0));

  // Add hosts at priority 1.
  std::vector<Upstream::HostSharedPtr> hosts_p1;
  ASSERT_TRUE(
      addSimpleHosts(*cluster, {"127.0.0.1:10002", "127.0.0.1:10003"}, {1, 1}, hosts_p1, 1));

  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());
  auto* lb_ptr = static_cast<void*>(lb_instance.get());

  // Verify hosts at each priority level.
  EXPECT_EQ(1, envoy_dynamic_module_callback_cluster_lb_get_hosts_count(lb_ptr, 0));
  EXPECT_EQ(2, envoy_dynamic_module_callback_cluster_lb_get_hosts_count(lb_ptr, 1));

  // Verify priority set size.
  EXPECT_EQ(2, envoy_dynamic_module_callback_cluster_lb_get_priority_set_size(lb_ptr));
}

// Test add_hosts with priority via ABI callback.
TEST_F(DynamicModuleClusterTest, AddHostsWithPriorityABI) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* cluster_ptr = static_cast<void*>(cluster.get());

  // Add a host at priority 0.
  envoy_dynamic_module_type_module_buffer addr0 = {"127.0.0.1:10001", 15};
  uint32_t weight0 = 1;
  envoy_dynamic_module_type_module_buffer empty = {"", 0};
  envoy_dynamic_module_type_cluster_host_envoy_ptr host0 = nullptr;
  ASSERT_TRUE(envoy_dynamic_module_callback_cluster_add_hosts(
      cluster_ptr, 0, &addr0, &weight0, &empty, &empty, &empty, nullptr, 0, 1, &host0));
  EXPECT_NE(nullptr, host0);

  // Add a host at priority 1.
  envoy_dynamic_module_type_module_buffer addr1 = {"127.0.0.1:10002", 15};
  uint32_t weight1 = 2;
  envoy_dynamic_module_type_cluster_host_envoy_ptr host1 = nullptr;
  ASSERT_TRUE(envoy_dynamic_module_callback_cluster_add_hosts(
      cluster_ptr, 1, &addr1, &weight1, &empty, &empty, &empty, nullptr, 0, 1, &host1));
  EXPECT_NE(nullptr, host1);

  // Verify via LB.
  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());
  auto* lb_ptr = static_cast<void*>(lb_instance.get());

  EXPECT_EQ(1, envoy_dynamic_module_callback_cluster_lb_get_hosts_count(lb_ptr, 0));
  EXPECT_EQ(1, envoy_dynamic_module_callback_cluster_lb_get_hosts_count(lb_ptr, 1));
  EXPECT_EQ(2, envoy_dynamic_module_callback_cluster_lb_get_priority_set_size(lb_ptr));

  // Verify hosts can be found across priorities.
  envoy_dynamic_module_type_module_buffer search0 = {"127.0.0.1:10001", 15};
  EXPECT_EQ(host0, envoy_dynamic_module_callback_cluster_lb_find_host_by_address(lb_ptr, search0));
  envoy_dynamic_module_type_module_buffer search1 = {"127.0.0.1:10002", 15};
  EXPECT_EQ(host1, envoy_dynamic_module_callback_cluster_lb_find_host_by_address(lb_ptr, search1));
}

// Test add_hosts with locality and priority via ABI callback.
TEST_F(DynamicModuleClusterTest, AddHostsWithLocalityAndPriorityABI) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* cluster_ptr = static_cast<void*>(cluster.get());

  // Add a host at priority 1 with locality.
  envoy_dynamic_module_type_module_buffer addr = {"127.0.0.1:10001", 15};
  uint32_t weight = 1;
  envoy_dynamic_module_type_module_buffer region = {"us-east-1", 9};
  envoy_dynamic_module_type_module_buffer zone = {"zone-a", 6};
  envoy_dynamic_module_type_module_buffer sub_zone = {"", 0};
  envoy_dynamic_module_type_cluster_host_envoy_ptr host_ptr = nullptr;

  ASSERT_TRUE(envoy_dynamic_module_callback_cluster_add_hosts(
      cluster_ptr, 1, &addr, &weight, &region, &zone, &sub_zone, nullptr, 0, 1, &host_ptr));
  EXPECT_NE(nullptr, host_ptr);

  // Verify via LB.
  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());
  auto* lb_ptr = static_cast<void*>(lb_instance.get());

  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_lb_get_hosts_count(lb_ptr, 0));
  EXPECT_EQ(1, envoy_dynamic_module_callback_cluster_lb_get_hosts_count(lb_ptr, 1));

  // Verify locality is correct at priority 1.
  envoy_dynamic_module_type_envoy_buffer result_region = {}, result_zone = {}, result_sub_zone = {};
  EXPECT_TRUE(envoy_dynamic_module_callback_cluster_lb_get_host_locality(
      lb_ptr, 1, 0, &result_region, &result_zone, &result_sub_zone));
  EXPECT_EQ("us-east-1", std::string(result_region.ptr, result_region.length));
  EXPECT_EQ("zone-a", std::string(result_zone.ptr, result_zone.length));
}

// Test that update_host_health works correctly for hosts at non-zero priority levels.
TEST_F(DynamicModuleClusterTest, UpdateHostHealthAtNonZeroPriority) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);

  // Add hosts at priority 0 and priority 1.
  std::vector<Upstream::HostSharedPtr> hosts_p0;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10001"}, {1}, hosts_p0, 0));
  std::vector<Upstream::HostSharedPtr> hosts_p1;
  ASSERT_TRUE(
      addSimpleHosts(*cluster, {"127.0.0.1:10002", "127.0.0.1:10003"}, {1, 1}, hosts_p1, 1));

  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, cluster->prioritySet());
  auto* lb_ptr = static_cast<void*>(lb_instance.get());

  // All hosts should be healthy initially.
  EXPECT_EQ(1, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(lb_ptr, 0));
  EXPECT_EQ(2, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(lb_ptr, 1));

  // Mark a host at priority 1 as unhealthy.
  EXPECT_TRUE(
      cluster->updateHostHealth(hosts_p1[0], envoy_dynamic_module_type_host_health_Unhealthy));

  // Priority 0 should be unaffected, priority 1 should now have 1 healthy host.
  EXPECT_EQ(1, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(lb_ptr, 0));
  EXPECT_EQ(1, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(lb_ptr, 1));

  // Total hosts at priority 1 should still be 2.
  EXPECT_EQ(2, envoy_dynamic_module_callback_cluster_lb_get_hosts_count(lb_ptr, 1));

  // Restore health at priority 1.
  EXPECT_TRUE(
      cluster->updateHostHealth(hosts_p1[0], envoy_dynamic_module_type_host_health_Healthy));
  EXPECT_EQ(2, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(lb_ptr, 1));
}

// The load balancer must register its membership update callback on the priority set supplied at
// construction, which production callers always fill with the worker local set from
// ``LoadBalancerParams``. Reference equality against the constructor argument proves the
// subscription target and would fail if the registration moved back to the cluster main set.
TEST_F(DynamicModuleClusterTest, LbMemberUpdateCbRegistersOnWorkerLocalPrioritySet) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);

  Upstream::PrioritySetImpl worker_local;
  auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, worker_local);

  EXPECT_EQ(&lb_instance->memberUpdatePrioritySet(), &worker_local);
  EXPECT_NE(&lb_instance->memberUpdatePrioritySet(), &cluster->prioritySet());
}

// Repeated construct, update, destroy cycle on the worker local priority set to cover the
// subscribe and unsubscribe paths under sanitizer builds.
TEST_F(DynamicModuleClusterTest, LbRepeatedConstructTeardownWithUpdates) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);

  Upstream::PrioritySetImpl worker_priority_set;
  worker_priority_set.getOrCreateHostSet(0);

  std::vector<Upstream::HostSharedPtr> hosts;
  ASSERT_TRUE(addSimpleHosts(*cluster, {"127.0.0.1:10101", "127.0.0.1:10102"}, {1, 1}, hosts));

  for (int i = 0; i < 16; ++i) {
    auto handle = std::make_shared<DynamicModuleClusterHandle>(cluster);
    auto lb_instance = std::make_unique<DynamicModuleLoadBalancer>(handle, worker_priority_set);

    Upstream::HostVectorSharedPtr all_hosts(new Upstream::HostVector{hosts[0]});
    worker_priority_set.updateHosts(
        0,
        Upstream::HostSetImpl::partitionHosts(all_hosts, Upstream::HostsPerLocalityImpl::empty()),
        {}, {hosts[0]}, {}, absl::nullopt, absl::nullopt);

    all_hosts = std::make_shared<Upstream::HostVector>(Upstream::HostVector{hosts[0], hosts[1]});
    worker_priority_set.updateHosts(
        0,
        Upstream::HostSetImpl::partitionHosts(all_hosts, Upstream::HostsPerLocalityImpl::empty()),
        {}, {hosts[1]}, {}, absl::nullopt, absl::nullopt);
  }
}

// =============================================================================
// Off-main-thread guard tests for cluster ABI callbacks.
// =============================================================================

// Verifies that `cluster_add_hosts` is fail-closed when called off the main thread.
TEST_F(DynamicModuleClusterTest, AddHostsOffMainThreadFailsClosed) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  envoy_dynamic_module_type_module_buffer addr = {"127.0.0.1:10001", 15};
  uint32_t weight = 1;
  envoy_dynamic_module_type_module_buffer empty = {"", 0};
  envoy_dynamic_module_type_cluster_host_envoy_ptr host_ptr = nullptr;
  void* cluster_ptr = cluster.get();

  EXPECT_ENVOY_BUG(
      {
        std::thread t([&] {
          EXPECT_FALSE(envoy_dynamic_module_callback_cluster_add_hosts(
              cluster_ptr, 0, &addr, &weight, &empty, &empty, &empty, nullptr, 0, 1, &host_ptr));
        });
        t.join();
      },
      "envoy_dynamic_module_callback_cluster_add_hosts must be called on the main thread");
}

// Verifies that `cluster_remove_hosts` is fail-closed when called off the main thread.
TEST_F(DynamicModuleClusterTest, RemoveHostsOffMainThreadFailsClosed) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  envoy_dynamic_module_type_cluster_host_envoy_ptr host_ptrs[1] = {nullptr};
  void* cluster_ptr = cluster.get();

  EXPECT_ENVOY_BUG(
      {
        std::thread t([&] {
          EXPECT_EQ(0u,
                    envoy_dynamic_module_callback_cluster_remove_hosts(cluster_ptr, host_ptrs, 1));
        });
        t.join();
      },
      "envoy_dynamic_module_callback_cluster_remove_hosts must be called on the main thread");
}

// Verifies that `cluster_update_host_health` is fail-closed when called off the main thread.
TEST_F(DynamicModuleClusterTest, UpdateHostHealthOffMainThreadFailsClosed) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  void* cluster_ptr = cluster.get();

  EXPECT_ENVOY_BUG(
      {
        std::thread t([&] {
          EXPECT_FALSE(envoy_dynamic_module_callback_cluster_update_host_health(
              cluster_ptr, nullptr, envoy_dynamic_module_type_host_health_Healthy));
        });
        t.join();
      },
      "envoy_dynamic_module_callback_cluster_update_host_health must be called on the main "
      "thread");
}

// Verifies that `cluster_pre_init_complete` is a safe no-op when called off the main thread.
TEST_F(DynamicModuleClusterTest, PreInitCompleteOffMainThreadFailsClosed) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  ASSERT_NE(nullptr, cluster);

  void* cluster_ptr = cluster.get();

  EXPECT_ENVOY_BUG(
      {
        std::thread t(
            [&] { envoy_dynamic_module_callback_cluster_pre_init_complete(cluster_ptr); });
        t.join();
      },
      "envoy_dynamic_module_callback_cluster_pre_init_complete must be called on the main thread");
}

// Verifies the factory auto-freezes stat creation so `define_*` returns `Frozen` after init.
TEST_F(DynamicModuleClusterTest, MetricsFrozenAfterInit) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* config = cluster->config().get();
  EXPECT_TRUE(config->stat_creation_frozen_);

  envoy_dynamic_module_type_module_buffer name = {const_cast<char*>("frozen_counter"), 14};
  envoy_dynamic_module_type_module_buffer label = {const_cast<char*>("label"), 5};
  size_t out_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Frozen,
            envoy_dynamic_module_callback_cluster_config_define_counter(config, name, nullptr, 0,
                                                                        &out_id));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Frozen,
            envoy_dynamic_module_callback_cluster_config_define_counter(config, name, &label, 1,
                                                                        &out_id));
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_Frozen,
      envoy_dynamic_module_callback_cluster_config_define_gauge(config, name, nullptr, 0, &out_id));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Frozen,
            envoy_dynamic_module_callback_cluster_config_define_histogram(config, name, nullptr, 0,
                                                                          &out_id));
}

// Drives concurrent labeled increments from multiple threads to verify no data race in the
// shared `stat_name_pool_`. Run under `--config=tsan` to verify.
TEST_F(DynamicModuleClusterTest, MetricsConcurrentIncrementCounterVecNoRace) {
  auto result = createCluster(makeYamlConfig("cluster_no_op"));
  ASSERT_TRUE(result.ok()) << result.status().message();
  auto cluster = std::dynamic_pointer_cast<DynamicModuleCluster>(result->first);
  auto* config = cluster->config().get();
  unfreezeStatCreation(*config);

  envoy_dynamic_module_type_module_buffer name = {const_cast<char*>("race_counter"), 12};
  envoy_dynamic_module_type_module_buffer label_names = {const_cast<char*>("status"), 6};
  size_t counter_id = 0;
  ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_cluster_config_define_counter(config, name, &label_names,
                                                                        1, &counter_id));

  constexpr int kNumThreads = 8;
  constexpr int kIncrementsPerThread = 2000;

  // Pre-warm the test scope's counter cache so workers only hit the cache. `TestScope` uses an
  // unsynchronized map for counter caching that would otherwise race independently of the path
  // under test.
  for (int t = 0; t < kNumThreads; ++t) {
    const std::string label_value_str = absl::StrCat("worker_", t);
    envoy_dynamic_module_type_module_buffer label_value = {
        const_cast<char*>(label_value_str.data()), label_value_str.size()};
    ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success,
              envoy_dynamic_module_callback_cluster_config_increment_counter(config, counter_id,
                                                                             &label_value, 1, 0));
  }

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&, t]() {
      const std::string label_value_str = absl::StrCat("worker_", t);
      envoy_dynamic_module_type_module_buffer label_value = {
          const_cast<char*>(label_value_str.data()), label_value_str.size()};
      ready.fetch_add(1, std::memory_order_relaxed);
      while (!go.load(std::memory_order_acquire)) {
      }
      for (int i = 0; i < kIncrementsPerThread; ++i) {
        ASSERT_EQ(envoy_dynamic_module_type_metrics_result_Success,
                  envoy_dynamic_module_callback_cluster_config_increment_counter(
                      config, counter_id, &label_value, 1, 1));
      }
    });
  }
  while (ready.load(std::memory_order_acquire) < kNumThreads) {
  }
  go.store(true, std::memory_order_release);
  for (auto& th : threads) {
    th.join();
  }
}

} // namespace
} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
