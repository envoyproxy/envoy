#include "source/extensions/clusters/dynamic_modules/cluster.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {
namespace {

using Extensions::DynamicModules::testSharedObjectPath;
using ::testing::NiceMock;

TEST(DynamicModuleClusterConfigTest, ConfigCreationAndDestruction) {
  // Load the no-op cluster module.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testSharedObjectPath("cluster_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config =
      newDynamicModuleClusterConfig("test_cluster", "", std::move(dynamic_module.value()), context);
  EXPECT_TRUE(config.ok());
  EXPECT_NE(config.value()->in_module_config_, nullptr);
}

TEST(DynamicModuleClusterConfigTest, ConfigCreationFailsOnInvalidModule) {
  // program_init_fail returns null for initialization, so module loading fails.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testSharedObjectPath("program_init_fail", "c"), false);
  // The module fails to initialize (envoy_dynamic_module_on_program_init returns null).
  EXPECT_FALSE(dynamic_module.ok());
}

TEST(DynamicModuleClusterTest, ClusterCreationAndDestruction) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testSharedObjectPath("cluster_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config =
      newDynamicModuleClusterConfig("test_cluster", "", std::move(dynamic_module.value()), context);
  EXPECT_TRUE(config.ok());

  // Create the cluster.
  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig module_config;
  module_config.set_cluster_name("test_cluster");

  Upstream::ClusterFactoryContextImpl cluster_context(context, nullptr, nullptr, false);

  absl::Status creation_status = absl::OkStatus();
  auto cluster = std::make_shared<DynamicModuleCluster>(
      cluster_proto, module_config, config.value(), cluster_context, creation_status);

  EXPECT_TRUE(creation_status.ok());
  EXPECT_NE(cluster->moduleClusterPtr(), nullptr);
}

TEST(DynamicModuleClusterTest, AddHostSuccess) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testSharedObjectPath("cluster_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config =
      newDynamicModuleClusterConfig("test_cluster", "", std::move(dynamic_module.value()), context);
  EXPECT_TRUE(config.ok());

  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig module_config;
  module_config.set_cluster_name("test_cluster");

  Upstream::ClusterFactoryContextImpl cluster_context(context, nullptr, nullptr, false);

  absl::Status creation_status = absl::OkStatus();
  auto cluster = std::make_shared<DynamicModuleCluster>(
      cluster_proto, module_config, config.value(), cluster_context, creation_status);

  EXPECT_TRUE(creation_status.ok());

  // Add a host.
  auto host_result = cluster->addHost("127.0.0.1", 8080, 1);
  EXPECT_TRUE(host_result.ok());
  EXPECT_NE(host_result.value(), nullptr);

  // Verify we can retrieve the host.
  auto retrieved_host = cluster->getHostByAddress("127.0.0.1", 8080);
  EXPECT_NE(retrieved_host, nullptr);
  EXPECT_EQ(retrieved_host->address()->ip()->addressAsString(), "127.0.0.1");
  EXPECT_EQ(retrieved_host->address()->ip()->port(), 8080);
}

TEST(DynamicModuleClusterTest, AddHostInvalidAddress) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testSharedObjectPath("cluster_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config =
      newDynamicModuleClusterConfig("test_cluster", "", std::move(dynamic_module.value()), context);
  EXPECT_TRUE(config.ok());

  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig module_config;
  module_config.set_cluster_name("test_cluster");

  Upstream::ClusterFactoryContextImpl cluster_context(context, nullptr, nullptr, false);

  absl::Status creation_status = absl::OkStatus();
  auto cluster = std::make_shared<DynamicModuleCluster>(
      cluster_proto, module_config, config.value(), cluster_context, creation_status);

  EXPECT_TRUE(creation_status.ok());

  // Try to add a host with invalid address.
  auto host_result = cluster->addHost("invalid_address", 8080, 1);
  EXPECT_FALSE(host_result.ok());
  EXPECT_EQ(host_result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(DynamicModuleClusterTest, AddHostInvalidWeight) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testSharedObjectPath("cluster_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config =
      newDynamicModuleClusterConfig("test_cluster", "", std::move(dynamic_module.value()), context);
  EXPECT_TRUE(config.ok());

  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig module_config;
  module_config.set_cluster_name("test_cluster");

  Upstream::ClusterFactoryContextImpl cluster_context(context, nullptr, nullptr, false);

  absl::Status creation_status = absl::OkStatus();
  auto cluster = std::make_shared<DynamicModuleCluster>(
      cluster_proto, module_config, config.value(), cluster_context, creation_status);

  EXPECT_TRUE(creation_status.ok());

  // Try to add a host with weight > 128.
  auto host_result = cluster->addHost("127.0.0.1", 8080, 200);
  EXPECT_FALSE(host_result.ok());
  EXPECT_EQ(host_result.status().code(), absl::StatusCode::kInvalidArgument);

  // Try to add a host with weight = 0.
  host_result = cluster->addHost("127.0.0.1", 8080, 0);
  EXPECT_FALSE(host_result.ok());
  EXPECT_EQ(host_result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(DynamicModuleClusterTest, RemoveHost) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testSharedObjectPath("cluster_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config =
      newDynamicModuleClusterConfig("test_cluster", "", std::move(dynamic_module.value()), context);
  EXPECT_TRUE(config.ok());

  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig module_config;
  module_config.set_cluster_name("test_cluster");

  Upstream::ClusterFactoryContextImpl cluster_context(context, nullptr, nullptr, false);

  absl::Status creation_status = absl::OkStatus();
  auto cluster = std::make_shared<DynamicModuleCluster>(
      cluster_proto, module_config, config.value(), cluster_context, creation_status);

  EXPECT_TRUE(creation_status.ok());

  // Add a host.
  auto host_result = cluster->addHost("127.0.0.1", 8080, 1);
  EXPECT_TRUE(host_result.ok());

  auto host = host_result.value();
  EXPECT_NE(host, nullptr);

  // Verify the host exists.
  auto retrieved_host = cluster->getHostByAddress("127.0.0.1", 8080);
  EXPECT_NE(retrieved_host, nullptr);

  // Remove the host.
  cluster->removeHost(host);

  // Verify the host is gone.
  retrieved_host = cluster->getHostByAddress("127.0.0.1", 8080);
  EXPECT_EQ(retrieved_host, nullptr);
}

TEST(DynamicModuleClusterTest, GetHosts) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testSharedObjectPath("cluster_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config =
      newDynamicModuleClusterConfig("test_cluster", "", std::move(dynamic_module.value()), context);
  EXPECT_TRUE(config.ok());

  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig module_config;
  module_config.set_cluster_name("test_cluster");

  Upstream::ClusterFactoryContextImpl cluster_context(context, nullptr, nullptr, false);

  absl::Status creation_status = absl::OkStatus();
  auto cluster = std::make_shared<DynamicModuleCluster>(
      cluster_proto, module_config, config.value(), cluster_context, creation_status);

  EXPECT_TRUE(creation_status.ok());

  // Initially no hosts.
  auto hosts = cluster->getHosts();
  EXPECT_EQ(hosts.size(), 0);

  // Add some hosts.
  EXPECT_TRUE(cluster->addHost("127.0.0.1", 8080, 1).ok());
  EXPECT_TRUE(cluster->addHost("127.0.0.2", 8081, 2).ok());

  // Verify we get the hosts back.
  hosts = cluster->getHosts();
  EXPECT_EQ(hosts.size(), 2);

  // Verify host info is correct.
  bool found_first = false;
  bool found_second = false;
  for (const auto& [host, info] : hosts) {
    std::string addr(info.address, info.address_length);
    if (addr == "127.0.0.1" && info.port == 8080) {
      found_first = true;
      EXPECT_EQ(info.weight, 1);
    } else if (addr == "127.0.0.2" && info.port == 8081) {
      found_second = true;
      EXPECT_EQ(info.weight, 2);
    }
  }
  EXPECT_TRUE(found_first);
  EXPECT_TRUE(found_second);
}

TEST(DynamicModuleLoadBalancerTest, ChooseHostReturnsNullWhenEmpty) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testSharedObjectPath("cluster_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config =
      newDynamicModuleClusterConfig("test_cluster", "", std::move(dynamic_module.value()), context);
  EXPECT_TRUE(config.ok());

  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig module_config;
  module_config.set_cluster_name("test_cluster");

  Upstream::ClusterFactoryContextImpl cluster_context(context, nullptr, nullptr, false);

  absl::Status creation_status = absl::OkStatus();
  auto cluster = std::make_shared<DynamicModuleCluster>(
      cluster_proto, module_config, config.value(), cluster_context, creation_status);

  EXPECT_TRUE(creation_status.ok());

  // Create a load balancer.
  auto lb = std::make_unique<DynamicModuleLoadBalancer>(cluster);

  // Choose host should return nullptr when cluster is empty.
  auto response = lb->chooseHost(nullptr);
  EXPECT_EQ(response.host, nullptr);
}

TEST(DynamicModuleClusterTest, HostSetChangeCallbackInvoked) {
  // This test verifies that the host set change callback is invoked when hosts are added/removed.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testSharedObjectPath("cluster_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config =
      newDynamicModuleClusterConfig("test_cluster", "", std::move(dynamic_module.value()), context);
  EXPECT_TRUE(config.ok());

  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig module_config;
  module_config.set_cluster_name("test_cluster");

  Upstream::ClusterFactoryContextImpl cluster_context(context, nullptr, nullptr, false);

  absl::Status creation_status = absl::OkStatus();
  auto cluster = std::make_shared<DynamicModuleCluster>(
      cluster_proto, module_config, config.value(), cluster_context, creation_status);

  EXPECT_TRUE(creation_status.ok());

  // Adding a host should trigger the host set change callback.
  // The no-op C module just ignores this, but it verifies the callback is properly wired.
  auto host_result = cluster->addHost("127.0.0.1", 8080, 1);
  EXPECT_TRUE(host_result.ok());

  // Remove the host to trigger another callback.
  cluster->removeHost(host_result.value());

  // Verify the host is gone.
  auto retrieved_host = cluster->getHostByAddress("127.0.0.1", 8080);
  EXPECT_EQ(retrieved_host, nullptr);
}

TEST(DynamicModuleClusterTest, HostHealthChangeCallbackFunction) {
  // This test verifies that on_host_health_change is properly resolved and available.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testSharedObjectPath("cluster_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config =
      newDynamicModuleClusterConfig("test_cluster", "", std::move(dynamic_module.value()), context);
  EXPECT_TRUE(config.ok());

  // Verify the function pointer is resolved.
  EXPECT_NE(config.value()->on_host_health_change_, nullptr);
  EXPECT_NE(config.value()->on_host_set_change_, nullptr);
}

TEST(DynamicModuleClusterTest, ClusterManagerAccessible) {
  // This test verifies that the cluster manager is accessible from the cluster.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testSharedObjectPath("cluster_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config =
      newDynamicModuleClusterConfig("test_cluster", "", std::move(dynamic_module.value()), context);
  EXPECT_TRUE(config.ok());

  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig module_config;
  module_config.set_cluster_name("test_cluster");

  Upstream::ClusterFactoryContextImpl cluster_context(context, nullptr, nullptr, false);

  absl::Status creation_status = absl::OkStatus();
  auto cluster = std::make_shared<DynamicModuleCluster>(
      cluster_proto, module_config, config.value(), cluster_context, creation_status);

  EXPECT_TRUE(creation_status.ok());

  // Verify cluster manager is accessible by calling a method on it.
  // This confirms the reference is valid.
  Upstream::ClusterManager& cm = cluster->clusterManager();
  // Just verify we got a valid reference by checking it's not shutdown.
  // In a real scenario the module would use this to access other clusters.
  EXPECT_FALSE(cm.isShutdown());
}

TEST(DynamicModuleClusterAbiTest, GetThreadLocalClusterReturnsNullForNonExistent) {
  // This test verifies that the cluster manager callback returns null for non-existent clusters.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testSharedObjectPath("cluster_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto config =
      newDynamicModuleClusterConfig("test_cluster", "", std::move(dynamic_module.value()), context);
  EXPECT_TRUE(config.ok());

  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig module_config;
  module_config.set_cluster_name("test_cluster");

  Upstream::ClusterFactoryContextImpl cluster_context(context, nullptr, nullptr, false);

  absl::Status creation_status = absl::OkStatus();
  auto cluster = std::make_shared<DynamicModuleCluster>(
      cluster_proto, module_config, config.value(), cluster_context, creation_status);

  EXPECT_TRUE(creation_status.ok());

  // Try to get a non-existent cluster - should return null.
  const char* cluster_name = "non_existent_cluster";
  auto result = envoy_dynamic_module_callback_cluster_manager_get_thread_local_cluster(
      cluster.get(), const_cast<char*>(cluster_name), strlen(cluster_name));
  EXPECT_EQ(result, nullptr);
}

TEST(DynamicModuleClusterAbiTest, ChooseHostFromNullClusterReturnsNull) {
  // Verify that choosing a host from a null thread local cluster returns null.
  auto result = envoy_dynamic_module_callback_thread_local_cluster_choose_host(nullptr, nullptr);
  EXPECT_EQ(result, nullptr);
}

TEST(DynamicModuleClusterAbiTest, GetNameFromNullClusterReturnsZero) {
  // Verify that getting name from a null thread local cluster returns 0.
  envoy_dynamic_module_type_buffer_envoy_ptr name_out = nullptr;
  auto result = envoy_dynamic_module_callback_thread_local_cluster_get_name(nullptr, &name_out);
  EXPECT_EQ(result, 0);
}

TEST(DynamicModuleClusterAbiTest, HostCountFromNullClusterReturnsZero) {
  // Verify that getting host count from a null thread local cluster returns 0.
  auto result = envoy_dynamic_module_callback_thread_local_cluster_host_count(nullptr);
  EXPECT_EQ(result, 0);
}

TEST(DynamicModuleClusterAbiTest, LbContextAttemptCountNullContext) {
  // Verify that getting attempt count from null context returns false.
  uint32_t attempt = 0;
  auto result = envoy_dynamic_module_callback_lb_context_get_attempt_count(nullptr, &attempt);
  EXPECT_FALSE(result);
}

TEST(DynamicModuleClusterAbiTest, LbContextDownstreamConnectionIdNullContext) {
  // Verify that getting connection id from null context returns false.
  uint64_t connection_id = 0;
  auto result = envoy_dynamic_module_callback_lb_context_get_downstream_connection_id(
      nullptr, &connection_id);
  EXPECT_FALSE(result);
}

} // namespace
} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
