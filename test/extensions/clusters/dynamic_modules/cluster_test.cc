#include "source/common/upstream/cluster_factory_impl.h"
#include "source/extensions/clusters/dynamic_modules/cluster.h"
#include "source/extensions/clusters/dynamic_modules/cluster_config.h"
#include "source/extensions/clusters/dynamic_modules/factory.h"
#include "source/extensions/load_balancing_policies/cluster_provided/config.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/options.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

class DynamicModuleClusterConfigTest : public testing::Test {
public:
  void SetUp() override {
    module_path_ = Envoy::Extensions::DynamicModules::testSharedObjectPath("cluster_no_op", "c");
  }

protected:
  std::string module_path_;
};

TEST_F(DynamicModuleClusterConfigTest, ConfigCreationAndDestruction) {
  auto module = Envoy::Extensions::DynamicModules::newDynamicModule(module_path_, false);
  ASSERT_TRUE(module.ok());

  auto config = DynamicModuleClusterConfig::newDynamicModuleClusterConfig(
      "test_cluster", "test_config", std::move(module.value()));
  ASSERT_TRUE(config.ok());

  // Verify the function pointers are resolved.
  EXPECT_NE(config.value()->on_cluster_config_destroy_, nullptr);
  EXPECT_NE(config.value()->on_cluster_new_, nullptr);
  EXPECT_NE(config.value()->on_cluster_destroy_, nullptr);
  EXPECT_NE(config.value()->on_cluster_init_, nullptr);
  EXPECT_NE(config.value()->on_cluster_cleanup_, nullptr);
  EXPECT_NE(config.value()->on_load_balancer_new_, nullptr);
  EXPECT_NE(config.value()->on_load_balancer_destroy_, nullptr);
  EXPECT_NE(config.value()->on_load_balancer_choose_host_, nullptr);
  EXPECT_NE(config.value()->on_host_set_change_, nullptr);
  EXPECT_NE(config.value()->on_host_health_change_, nullptr);
  EXPECT_NE(config.value()->in_module_config_, nullptr);
}

TEST_F(DynamicModuleClusterConfigTest, ConfigCreationFailsOnMissingSymbol) {
  // Test that loading a module without required symbols fails gracefully.
  // We use the http_no_op module which does not have cluster symbols.
  std::string http_module_path =
      Envoy::Extensions::DynamicModules::testSharedObjectPath("no_op", "c");
  auto module = Envoy::Extensions::DynamicModules::newDynamicModule(http_module_path, false);
  ASSERT_TRUE(module.ok());

  auto config = DynamicModuleClusterConfig::newDynamicModuleClusterConfig(
      "test_cluster", "test_config", std::move(module.value()));
  EXPECT_FALSE(config.ok());
}

TEST_F(DynamicModuleClusterConfigTest, ConfigCreationFailsWhenModuleReturnsNull) {
  // Test that config creation fails when the module's on_cluster_config_new returns null.
  std::string fail_module_path =
      Envoy::Extensions::DynamicModules::testSharedObjectPath("cluster_config_new_fail", "c");
  auto module = Envoy::Extensions::DynamicModules::newDynamicModule(fail_module_path, false);
  ASSERT_TRUE(module.ok());

  auto config = DynamicModuleClusterConfig::newDynamicModuleClusterConfig(
      "test_cluster", "test_config", std::move(module.value()));
  EXPECT_FALSE(config.ok());
  EXPECT_THAT(config.status().message(),
              testing::HasSubstr("Failed to create in-module cluster configuration"));
}

class DynamicModuleClusterFactoryTestBase {
protected:
  DynamicModuleClusterFactoryTestBase() {
    // Set up module search path.
    std::string shared_object_path =
        Envoy::Extensions::DynamicModules::testSharedObjectPath("cluster_no_op", "c");
    std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);

    ON_CALL(server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
  }

  envoy::config::cluster::v3::Cluster parseClusterConfig(const std::string& yaml) {
    return Upstream::parseClusterFromV3Yaml(yaml);
  }

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig
  parseModuleConfig(const envoy::config::cluster::v3::Cluster& cluster_config) {
    envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig config;
    THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
        cluster_config.cluster_type().typed_config(), ProtobufMessage::getStrictValidationVisitor(),
        config));
    return config;
  }

  Upstream::ClusterFactoryContextImpl createFactoryContext() {
    return Upstream::ClusterFactoryContextImpl(server_context_, nullptr, nullptr, false);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Stats::TestUtil::TestStore& stats_ = server_context_.store_;
  NiceMock<Random::MockRandomGenerator> random_;
  Api::ApiPtr api_ = Api::createApiForTest(stats_, random_);
};

// Test peer to access private factory method.
class DynamicModuleClusterFactoryTestPeer {
public:
  static absl::StatusOr<
      std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
  createClusterWithConfig(
      DynamicModuleClusterFactory& factory, const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context) {
    return factory.createClusterWithConfig(cluster, proto_config, context);
  }
};

class DynamicModuleClusterFactoryTest : public testing::Test,
                                        public DynamicModuleClusterFactoryTestBase {};

TEST_F(DynamicModuleClusterFactoryTest, InvalidLbPolicy) {
  const std::string yaml = R"EOF(
      name: dynamic_module_cluster
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

  auto cluster_config = parseClusterConfig(yaml);
  auto module_config = parseModuleConfig(cluster_config);
  auto factory_context = createFactoryContext();

  DynamicModuleClusterFactory factory;
  auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
      factory, cluster_config, module_config, factory_context);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(std::string(result.status().message()), testing::HasSubstr("CLUSTER_PROVIDED"));
}

TEST_F(DynamicModuleClusterFactoryTest, ModuleNotFound) {
  const std::string yaml = R"EOF(
      name: dynamic_module_cluster
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cluster_type:
        name: envoy.clusters.dynamic_modules
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
          dynamic_module_config:
            name: nonexistent_module
          cluster_name: test
    )EOF";

  auto cluster_config = parseClusterConfig(yaml);
  auto module_config = parseModuleConfig(cluster_config);
  auto factory_context = createFactoryContext();

  DynamicModuleClusterFactory factory;
  auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
      factory, cluster_config, module_config, factory_context);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(std::string(result.status().message()),
              testing::HasSubstr("Failed to load dynamic module"));
}

TEST_F(DynamicModuleClusterFactoryTest, SuccessfulCreation) {
  const std::string yaml = R"EOF(
      name: dynamic_module_cluster
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cluster_type:
        name: envoy.clusters.dynamic_modules
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
          dynamic_module_config:
            name: cluster_no_op
          cluster_name: test
          dynamic_host_discovery: true
          max_hosts: 100
    )EOF";

  auto cluster_config = parseClusterConfig(yaml);
  auto module_config = parseModuleConfig(cluster_config);
  auto factory_context = createFactoryContext();

  DynamicModuleClusterFactory factory;
  auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
      factory, cluster_config, module_config, factory_context);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_NE(result->first, nullptr);
  EXPECT_NE(result->second, nullptr);

  // Initialize the cluster.
  result->first->initialize([] { return absl::OkStatus(); });

  // Verify cluster info.
  EXPECT_EQ("dynamic_module_cluster", result->first->info()->name());
}

TEST_F(DynamicModuleClusterFactoryTest, WithStringValueConfig) {
  const std::string yaml = R"EOF(
      name: dynamic_module_cluster
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
            "@type": type.googleapis.com/google.protobuf.StringValue
            value: "my_config_string"
    )EOF";

  auto cluster_config = parseClusterConfig(yaml);
  auto module_config = parseModuleConfig(cluster_config);
  auto factory_context = createFactoryContext();

  DynamicModuleClusterFactory factory;
  auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
      factory, cluster_config, module_config, factory_context);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_NE(result->first, nullptr);
}

TEST_F(DynamicModuleClusterFactoryTest, WithBytesValueConfig) {
  const std::string yaml = R"EOF(
      name: dynamic_module_cluster
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
            value: "bXlfYnl0ZXM="
    )EOF";

  auto cluster_config = parseClusterConfig(yaml);
  auto module_config = parseModuleConfig(cluster_config);
  auto factory_context = createFactoryContext();

  DynamicModuleClusterFactory factory;
  auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
      factory, cluster_config, module_config, factory_context);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_NE(result->first, nullptr);
}

TEST_F(DynamicModuleClusterFactoryTest, WithStructConfig) {
  const std::string yaml = R"EOF(
      name: dynamic_module_cluster
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
              key1: "value1"
              key2: 123
    )EOF";

  auto cluster_config = parseClusterConfig(yaml);
  auto module_config = parseModuleConfig(cluster_config);
  auto factory_context = createFactoryContext();

  DynamicModuleClusterFactory factory;
  auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
      factory, cluster_config, module_config, factory_context);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_NE(result->first, nullptr);
}

TEST_F(DynamicModuleClusterFactoryTest, WithCleanupInterval) {
  const std::string yaml = R"EOF(
      name: dynamic_module_cluster
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cluster_type:
        name: envoy.clusters.dynamic_modules
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
          dynamic_module_config:
            name: cluster_no_op
          cluster_name: test
          cleanup_interval:
            seconds: 10
    )EOF";

  auto cluster_config = parseClusterConfig(yaml);
  auto module_config = parseModuleConfig(cluster_config);
  auto factory_context = createFactoryContext();

  DynamicModuleClusterFactory factory;
  auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
      factory, cluster_config, module_config, factory_context);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_NE(result->first, nullptr);
}

class DynamicModuleClusterFactoryFailureTest : public testing::Test {
protected:
  DynamicModuleClusterFactoryFailureTest() {
    ON_CALL(server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
  }

  envoy::config::cluster::v3::Cluster parseClusterConfig(const std::string& yaml) {
    return Upstream::parseClusterFromV3Yaml(yaml);
  }

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig
  parseModuleConfig(const envoy::config::cluster::v3::Cluster& cluster_config) {
    envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig config;
    THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
        cluster_config.cluster_type().typed_config(), ProtobufMessage::getStrictValidationVisitor(),
        config));
    return config;
  }

  Upstream::ClusterFactoryContextImpl createFactoryContext() {
    return Upstream::ClusterFactoryContextImpl(server_context_, nullptr, nullptr, false);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Stats::TestUtil::TestStore& stats_ = server_context_.store_;
  NiceMock<Random::MockRandomGenerator> random_;
  Api::ApiPtr api_ = Api::createApiForTest(stats_, random_);
};

TEST_F(DynamicModuleClusterFactoryFailureTest, ClusterNewReturnsNull) {
  // Set up the module search path to point to the cluster_new_fail module.
  std::string shared_object_path =
      Envoy::Extensions::DynamicModules::testSharedObjectPath("cluster_new_fail", "c");
  std::string shared_object_dir = std::filesystem::path(shared_object_path).parent_path().string();
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);

  const std::string yaml = R"EOF(
      name: dynamic_module_cluster
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cluster_type:
        name: envoy.clusters.dynamic_modules
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
          dynamic_module_config:
            name: cluster_new_fail
          cluster_name: test
    )EOF";

  auto cluster_config = parseClusterConfig(yaml);
  auto module_config = parseModuleConfig(cluster_config);
  auto factory_context = createFactoryContext();

  DynamicModuleClusterFactory factory;
  auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
      factory, cluster_config, module_config, factory_context);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(std::string(result.status().message()),
              testing::HasSubstr("Failed to create in-module cluster instance"));
}

// Test fixture for testing load balancer with null in_module_lb_.
class DynamicModuleClusterLbNewFailTest : public testing::Test {
protected:
  DynamicModuleClusterLbNewFailTest() {
    // Set up the module search path to point to the cluster_lb_new_fail module.
    std::string shared_object_path =
        Envoy::Extensions::DynamicModules::testSharedObjectPath("cluster_lb_new_fail", "c");
    std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);

    ON_CALL(server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
  }

  envoy::config::cluster::v3::Cluster parseClusterConfig(const std::string& yaml) {
    return Upstream::parseClusterFromV3Yaml(yaml);
  }

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig
  parseModuleConfig(const envoy::config::cluster::v3::Cluster& cluster_config) {
    envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig config;
    THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
        cluster_config.cluster_type().typed_config(), ProtobufMessage::getStrictValidationVisitor(),
        config));
    return config;
  }

  Upstream::ClusterFactoryContextImpl createFactoryContext() {
    return Upstream::ClusterFactoryContextImpl(server_context_, nullptr, nullptr, false);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Stats::TestUtil::TestStore& stats_ = server_context_.store_;
  NiceMock<Random::MockRandomGenerator> random_;
  Api::ApiPtr api_ = Api::createApiForTest(stats_, random_);
};

TEST_F(DynamicModuleClusterLbNewFailTest, ChooseHostReturnsNullWhenLbIsNull) {
  const std::string yaml = R"EOF(
      name: dynamic_module_cluster
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cluster_type:
        name: envoy.clusters.dynamic_modules
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
          dynamic_module_config:
            name: cluster_lb_new_fail
          cluster_name: test
    )EOF";

  auto cluster_config = parseClusterConfig(yaml);
  auto module_config = parseModuleConfig(cluster_config);
  auto factory_context = createFactoryContext();

  DynamicModuleClusterFactory factory;
  auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
      factory, cluster_config, module_config, factory_context);
  ASSERT_TRUE(result.ok()) << result.status().message();

  // Initialize the cluster.
  result->first->initialize([] { return absl::OkStatus(); });

  // Create load balancer - this will have null in_module_lb_ since the module returns null.
  auto lb_factory = result->second->factory();
  Upstream::LoadBalancerParams params{result->first->prioritySet(), nullptr};
  auto lb = lb_factory->create(params);

  // chooseHost should return null since in_module_lb_ is null.
  auto host_result = lb->chooseHost(nullptr);
  EXPECT_EQ(nullptr, host_result.host);
}

// Test peer class for accessing private members.
class DynamicModuleClusterTestPeer {
public:
  static DynamicModuleCluster* getCluster(Upstream::ClusterSharedPtr& cluster) {
    return dynamic_cast<DynamicModuleCluster*>(cluster.get());
  }

  static void callCleanup(DynamicModuleCluster* cluster) { cluster->cleanup(); }

  static void callOnHealthCheckComplete(DynamicModuleCluster* cluster, Upstream::HostSharedPtr host,
                                        Upstream::HealthTransition transition) {
    cluster->onHealthCheckComplete(host, transition);
  }
};

class DynamicModuleClusterTest : public testing::Test, public DynamicModuleClusterFactoryTestBase {
public:
  void SetUp() override {
    // Create a cluster for testing.
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
          dynamic_host_discovery: true
          max_hosts: 10
    )EOF";

    auto cluster_config = parseClusterConfig(yaml);
    auto module_config = parseModuleConfig(cluster_config);
    factory_context_ = std::make_unique<Upstream::ClusterFactoryContextImpl>(
        server_context_, nullptr, nullptr, false);

    DynamicModuleClusterFactory factory;
    auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
        factory, cluster_config, module_config, *factory_context_);
    ASSERT_TRUE(result.ok()) << result.status().message();
    cluster_ = result->first;
    lb_ = std::move(result->second);

    // Initialize the cluster.
    cluster_->initialize([] { return absl::OkStatus(); });

    dm_cluster_ = DynamicModuleClusterTestPeer::getCluster(cluster_);
    ASSERT_NE(dm_cluster_, nullptr);
  }

  void TearDown() override {
    // Reset in order to avoid destruction issues.
    lb_.reset();
    cluster_.reset();
  }

protected:
  std::unique_ptr<Upstream::ClusterFactoryContextImpl> factory_context_;
  Upstream::ClusterSharedPtr cluster_;
  Upstream::ThreadAwareLoadBalancerPtr lb_;
  DynamicModuleCluster* dm_cluster_;
};

TEST_F(DynamicModuleClusterTest, AddHostSuccess) {
  Upstream::HostSharedPtr result_host;
  auto error = dm_cluster_->addHost("127.0.0.1:8080", 1, &result_host);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_Ok, error);
  EXPECT_NE(result_host, nullptr);
  EXPECT_EQ("127.0.0.1:8080", result_host->address()->asString());
  EXPECT_EQ(1, result_host->weight());

  // Verify host is in priority set.
  EXPECT_EQ(1, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

TEST_F(DynamicModuleClusterTest, AddHostDuplicate) {
  Upstream::HostSharedPtr result_host1;
  auto error1 = dm_cluster_->addHost("127.0.0.1:8080", 1, &result_host1);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_Ok, error1);

  Upstream::HostSharedPtr result_host2;
  auto error2 = dm_cluster_->addHost("127.0.0.1:8080", 1, &result_host2);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_Ok, error2);

  // Should return the same host.
  EXPECT_EQ(result_host1.get(), result_host2.get());

  // Still only one host.
  EXPECT_EQ(1, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

TEST_F(DynamicModuleClusterTest, AddHostInvalidAddress) {
  Upstream::HostSharedPtr result_host;
  auto error = dm_cluster_->addHost("invalid_address", 1, &result_host);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_InvalidAddress, error);
  EXPECT_EQ(nullptr, result_host);
}

TEST_F(DynamicModuleClusterTest, AddHostInvalidWeight) {
  Upstream::HostSharedPtr result_host;

  // Weight 0 is invalid.
  auto error1 = dm_cluster_->addHost("127.0.0.1:8080", 0, &result_host);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_InvalidWeight, error1);

  // Weight > 128 is invalid.
  auto error2 = dm_cluster_->addHost("127.0.0.1:8080", 129, &result_host);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_InvalidWeight, error2);
}

TEST_F(DynamicModuleClusterTest, AddHostMaxHostsReached) {
  // Add max_hosts (10) hosts.
  for (int i = 0; i < 10; i++) {
    std::string addr = fmt::format("127.0.0.1:{}", 8080 + i);
    auto error = dm_cluster_->addHost(addr, 1, nullptr);
    EXPECT_EQ(envoy_dynamic_module_type_cluster_error_Ok, error);
  }

  // Adding one more should fail.
  auto error = dm_cluster_->addHost("127.0.0.1:9000", 1, nullptr);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_MaxHostsReached, error);
}

TEST_F(DynamicModuleClusterTest, RemoveHostSuccess) {
  Upstream::HostSharedPtr host;
  auto error1 = dm_cluster_->addHost("127.0.0.1:8080", 1, &host);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_Ok, error1);
  EXPECT_EQ(1, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());

  auto error2 = dm_cluster_->removeHost(host);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_Ok, error2);
  EXPECT_EQ(0, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

TEST_F(DynamicModuleClusterTest, GetHosts) {
  dm_cluster_->addHost("127.0.0.1:8080", 1, nullptr);
  dm_cluster_->addHost("127.0.0.1:8081", 2, nullptr);
  dm_cluster_->addHost("127.0.0.1:8082", 3, nullptr);

  auto hosts = dm_cluster_->getHosts();
  EXPECT_EQ(3, hosts.size());

  // Verify host info is populated.
  for (const auto& info : hosts) {
    EXPECT_NE(info.host, nullptr);
    EXPECT_GT(info.address.length, 0);
    EXPECT_GE(info.weight, 1);
    EXPECT_LE(info.weight, 3);
  }
}

TEST_F(DynamicModuleClusterTest, GetHostByAddress) {
  Upstream::HostSharedPtr added_host;
  dm_cluster_->addHost("127.0.0.1:8080", 1, &added_host);

  auto found_host = dm_cluster_->getHostByAddress("127.0.0.1:8080");
  EXPECT_NE(found_host, nullptr);
  EXPECT_EQ(added_host.get(), found_host.get());

  // Non-existent address.
  auto not_found = dm_cluster_->getHostByAddress("127.0.0.1:9999");
  EXPECT_EQ(nullptr, not_found);
}

TEST_F(DynamicModuleClusterTest, SetHostWeight) {
  Upstream::HostSharedPtr host;
  dm_cluster_->addHost("127.0.0.1:8080", 1, &host);
  EXPECT_EQ(1, host->weight());

  auto error = dm_cluster_->setHostWeight(host, 50);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_Ok, error);
  EXPECT_EQ(50, host->weight());
}

TEST_F(DynamicModuleClusterTest, SetHostWeightInvalid) {
  Upstream::HostSharedPtr host;
  dm_cluster_->addHost("127.0.0.1:8080", 1, &host);

  // Weight 0 is invalid.
  auto error1 = dm_cluster_->setHostWeight(host, 0);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_InvalidWeight, error1);

  // Weight > 128 is invalid.
  auto error2 = dm_cluster_->setHostWeight(host, 200);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_InvalidWeight, error2);
}

TEST_F(DynamicModuleClusterTest, ClusterManager) {
  EXPECT_EQ(&server_context_.cluster_manager_, &dm_cluster_->clusterManager());
}

TEST_F(DynamicModuleClusterTest, LoadBalancerMethods) {
  // Create load balancer.
  auto lb_factory = lb_->factory();
  Upstream::LoadBalancerParams params{cluster_->prioritySet(), nullptr};
  auto lb = lb_factory->create(params);

  // Test peekAnotherHost - always returns nullptr.
  EXPECT_EQ(nullptr, lb->peekAnotherHost(nullptr));

  // Test selectExistingConnection - always returns nullopt.
  std::vector<uint8_t> hash_data;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  EXPECT_FALSE(lb->selectExistingConnection(nullptr, *mock_host, hash_data).has_value());

  // Test lifetimeCallbacks - always returns empty.
  EXPECT_FALSE(lb->lifetimeCallbacks().has_value());
}

TEST_F(DynamicModuleClusterTest, LoadBalancerChooseHost) {
  // Add a host to the cluster.
  Upstream::HostSharedPtr host;
  dm_cluster_->addHost("127.0.0.1:8080", 1, &host);

  // Create load balancer.
  auto lb_factory = lb_->factory();
  Upstream::LoadBalancerParams params{cluster_->prioritySet(), nullptr};
  auto lb = lb_factory->create(params);

  // Choose host with nullptr context.
  auto result = lb->chooseHost(nullptr);
  // Note: the no_op module's load balancer returns nullptr.
  EXPECT_EQ(nullptr, result.host);
}

class DynamicModuleClusterAbiTest : public testing::Test,
                                    public DynamicModuleClusterFactoryTestBase {};

TEST_F(DynamicModuleClusterAbiTest, ClusterCallbacksNullHandling) {
  envoy_dynamic_module_type_envoy_buffer result_buffer{nullptr, 0};

  // Get name with null pointer.
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_get_name(nullptr, &result_buffer));

  // Add host with null pointer.
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_InvalidAddress,
            envoy_dynamic_module_callback_cluster_add_host(nullptr, {nullptr, 0}, 1, nullptr));

  // Remove host with null pointer.
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_HostNotFound,
            envoy_dynamic_module_callback_cluster_remove_host(nullptr, nullptr));

  // Get hosts with null pointer.
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_get_hosts(nullptr, nullptr, 0));

  // Get host by address with null pointer.
  EXPECT_EQ(nullptr,
            envoy_dynamic_module_callback_cluster_get_host_by_address(nullptr, {nullptr, 0}));

  // Set host weight with null pointer.
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_HostNotFound,
            envoy_dynamic_module_callback_cluster_host_set_weight(nullptr, nullptr, 1));

  // Get host address with null pointer.
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_host_get_address(nullptr, &result_buffer));

  // Get host health with null pointer.
  EXPECT_EQ(envoy_dynamic_module_type_host_health_Unknown,
            envoy_dynamic_module_callback_cluster_host_get_health(nullptr));

  // Get host weight with null pointer.
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_host_get_weight(nullptr));

  // Pre-init complete with null pointer should not crash.
  envoy_dynamic_module_callback_cluster_pre_init_complete(nullptr);
}

TEST_F(DynamicModuleClusterAbiTest, LoadBalancerContextCallbacksNullHandling) {
  envoy_dynamic_module_type_envoy_buffer result_buffer{nullptr, 0};
  uint64_t hash_key = 0;

  // Get hash key with null context.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_get_hash_key(nullptr, &hash_key));

  // Get hash key with null result pointer.
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_get_hash_key(&mock_context, nullptr));

  // Get header with null context.
  EXPECT_EQ(0, envoy_dynamic_module_callback_lb_context_get_header(nullptr, {nullptr, 0},
                                                                   &result_buffer));

  // Get override host with null context.
  EXPECT_EQ(0, envoy_dynamic_module_callback_lb_context_get_override_host(nullptr, &result_buffer));

  // Get attempt count with null context.
  EXPECT_EQ(0, envoy_dynamic_module_callback_lb_context_get_attempt_count(nullptr));

  // Get downstream connection ID with null context.
  EXPECT_EQ(0, envoy_dynamic_module_callback_lb_context_get_downstream_connection_id(nullptr));
}

TEST_F(DynamicModuleClusterAbiTest, LoadBalancerContextCallbacksWithMockContext) {
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  envoy_dynamic_module_type_envoy_buffer result_buffer{nullptr, 0};

  // Test with context that has no hash key.
  ON_CALL(mock_context, computeHashKey()).WillByDefault(testing::Return(absl::nullopt));
  uint64_t hash_key = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_get_hash_key(&mock_context, &hash_key));

  // Test with context that has a hash key.
  ON_CALL(mock_context, computeHashKey()).WillByDefault(testing::Return(absl::make_optional(42UL)));
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_context_get_hash_key(&mock_context, &hash_key));
  EXPECT_EQ(42UL, hash_key);

  // Test with context that has no downstream headers.
  ON_CALL(mock_context, downstreamHeaders()).WillByDefault(testing::Return(nullptr));
  EXPECT_EQ(0, envoy_dynamic_module_callback_lb_context_get_header(
                   &mock_context, {"test-header", 11}, &result_buffer));

  // Test with context that has no override host.
  ON_CALL(mock_context, overrideHostToSelect()).WillByDefault(testing::Return(absl::nullopt));
  EXPECT_EQ(
      0, envoy_dynamic_module_callback_lb_context_get_override_host(&mock_context, &result_buffer));

  // Test attempt count.
  ON_CALL(mock_context, hostSelectionRetryCount()).WillByDefault(testing::Return(3));
  EXPECT_EQ(3, envoy_dynamic_module_callback_lb_context_get_attempt_count(&mock_context));

  // Test with context that has no downstream connection.
  ON_CALL(mock_context, downstreamConnection()).WillByDefault(testing::Return(nullptr));
  EXPECT_EQ(0,
            envoy_dynamic_module_callback_lb_context_get_downstream_connection_id(&mock_context));
}

TEST_F(DynamicModuleClusterAbiTest, ClusterManagerCallbacksNullHandling) {
  // Get thread local cluster with null cluster manager.
  EXPECT_EQ(nullptr, envoy_dynamic_module_callback_cluster_manager_get_thread_local_cluster(
                         nullptr, {nullptr, 0}));

  // Choose host from null thread local cluster.
  EXPECT_EQ(nullptr,
            envoy_dynamic_module_callback_thread_local_cluster_choose_host(nullptr, nullptr));

  // Get name from null thread local cluster.
  envoy_dynamic_module_type_envoy_buffer result_buffer{nullptr, 0};
  EXPECT_EQ(0,
            envoy_dynamic_module_callback_thread_local_cluster_get_name(nullptr, &result_buffer));

  // Get host count from null thread local cluster.
  EXPECT_EQ(0, envoy_dynamic_module_callback_thread_local_cluster_host_count(nullptr));
}

// Test class that creates a cluster and tests ABI callbacks with actual cluster objects.
class DynamicModuleClusterAbiWithClusterTest : public DynamicModuleClusterTest {};

TEST_F(DynamicModuleClusterAbiWithClusterTest, ClusterGetName) {
  envoy_dynamic_module_type_envoy_buffer result_buffer{nullptr, 0};
  size_t size = envoy_dynamic_module_callback_cluster_get_name(dm_cluster_, &result_buffer);
  EXPECT_GT(size, 0);
  EXPECT_EQ(size, result_buffer.length);
  std::string name(result_buffer.ptr, result_buffer.length);
  EXPECT_EQ("test_cluster", name);
}

TEST_F(DynamicModuleClusterAbiWithClusterTest, ClusterAddHostViaAbi) {
  envoy_dynamic_module_type_host_envoy_ptr result_host = nullptr;
  std::string addr = "127.0.0.1:9090";
  envoy_dynamic_module_type_envoy_buffer addr_buffer{addr.data(), addr.size()};
  auto error =
      envoy_dynamic_module_callback_cluster_add_host(dm_cluster_, addr_buffer, 1, &result_host);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_Ok, error);
  EXPECT_NE(nullptr, result_host);

  // Verify host is in the cluster.
  EXPECT_EQ(1, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

TEST_F(DynamicModuleClusterAbiWithClusterTest, ClusterRemoveHostViaAbi) {
  // First add a host.
  Upstream::HostSharedPtr host;
  dm_cluster_->addHost("127.0.0.1:9090", 1, &host);
  EXPECT_EQ(1, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());

  // Now remove it via ABI.
  auto error = envoy_dynamic_module_callback_cluster_remove_host(dm_cluster_, host.get());
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_Ok, error);
  EXPECT_EQ(0, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

TEST_F(DynamicModuleClusterAbiWithClusterTest, ClusterGetHostsViaAbi) {
  // Add some hosts.
  dm_cluster_->addHost("127.0.0.1:8080", 1, nullptr);
  dm_cluster_->addHost("127.0.0.1:8081", 2, nullptr);

  // Get count first (hosts = nullptr).
  size_t count = envoy_dynamic_module_callback_cluster_get_hosts(dm_cluster_, nullptr, 0);
  EXPECT_EQ(2, count);

  // Get actual hosts.
  std::vector<envoy_dynamic_module_type_host_info> hosts(10);
  count = envoy_dynamic_module_callback_cluster_get_hosts(dm_cluster_, hosts.data(), 10);
  EXPECT_EQ(2, count);

  for (size_t i = 0; i < count; i++) {
    EXPECT_NE(nullptr, hosts[i].host);
    EXPECT_GT(hosts[i].address.length, 0);
  }
}

TEST_F(DynamicModuleClusterAbiWithClusterTest, ClusterGetHostByAddressViaAbi) {
  Upstream::HostSharedPtr added_host;
  dm_cluster_->addHost("127.0.0.1:8080", 1, &added_host);

  std::string addr = "127.0.0.1:8080";
  envoy_dynamic_module_type_envoy_buffer addr_buffer{addr.data(), addr.size()};
  auto* found = envoy_dynamic_module_callback_cluster_get_host_by_address(dm_cluster_, addr_buffer);
  EXPECT_EQ(added_host.get(), found);

  // Non-existent address.
  std::string addr2 = "127.0.0.1:9999";
  envoy_dynamic_module_type_envoy_buffer addr_buffer2{addr2.data(), addr2.size()};
  auto* not_found =
      envoy_dynamic_module_callback_cluster_get_host_by_address(dm_cluster_, addr_buffer2);
  EXPECT_EQ(nullptr, not_found);
}

TEST_F(DynamicModuleClusterAbiWithClusterTest, ClusterHostSetWeightViaAbi) {
  Upstream::HostSharedPtr host;
  dm_cluster_->addHost("127.0.0.1:8080", 1, &host);

  auto error = envoy_dynamic_module_callback_cluster_host_set_weight(dm_cluster_, host.get(), 50);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_Ok, error);
  EXPECT_EQ(50, host->weight());
}

TEST_F(DynamicModuleClusterAbiWithClusterTest, HostGetAddress) {
  Upstream::HostSharedPtr host;
  dm_cluster_->addHost("127.0.0.1:8080", 1, &host);

  envoy_dynamic_module_type_envoy_buffer result_buffer{nullptr, 0};
  size_t size = envoy_dynamic_module_callback_cluster_host_get_address(host.get(), &result_buffer);
  EXPECT_GT(size, 0);
  std::string addr(result_buffer.ptr, result_buffer.length);
  EXPECT_EQ("127.0.0.1:8080", addr);
}

TEST_F(DynamicModuleClusterAbiWithClusterTest, HostGetHealth) {
  Upstream::HostSharedPtr host;
  dm_cluster_->addHost("127.0.0.1:8080", 1, &host);

  auto health = envoy_dynamic_module_callback_cluster_host_get_health(host.get());
  // New hosts default to healthy.
  EXPECT_EQ(envoy_dynamic_module_type_host_health_Healthy, health);
}

TEST_F(DynamicModuleClusterAbiWithClusterTest, HostGetWeight) {
  Upstream::HostSharedPtr host;
  dm_cluster_->addHost("127.0.0.1:8080", 5, &host);

  uint32_t weight = envoy_dynamic_module_callback_cluster_host_get_weight(host.get());
  EXPECT_EQ(5, weight);
}

TEST_F(DynamicModuleClusterAbiWithClusterTest, LoadBalancerContextWithHeaders) {
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  Http::TestRequestHeaderMapImpl headers{{"x-test-header", "test-value"}};

  ON_CALL(mock_context, downstreamHeaders()).WillByDefault(testing::Return(&headers));

  envoy_dynamic_module_type_envoy_buffer result_buffer{nullptr, 0};
  std::string key = "x-test-header";
  envoy_dynamic_module_type_envoy_buffer key_buffer{key.data(), key.size()};
  size_t size = envoy_dynamic_module_callback_lb_context_get_header(&mock_context, key_buffer,
                                                                    &result_buffer);
  EXPECT_GT(size, 0);
  std::string value(result_buffer.ptr, result_buffer.length);
  EXPECT_EQ("test-value", value);

  // Non-existent header.
  std::string key2 = "x-nonexistent";
  envoy_dynamic_module_type_envoy_buffer key_buffer2{key2.data(), key2.size()};
  size = envoy_dynamic_module_callback_lb_context_get_header(&mock_context, key_buffer2,
                                                             &result_buffer);
  EXPECT_EQ(0, size);
}

TEST_F(DynamicModuleClusterAbiWithClusterTest, LoadBalancerContextWithOverrideHost) {
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;

  // Test with override host.
  std::string override_host_str = "override.example.com";
  Upstream::LoadBalancerContext::OverrideHost override_host{absl::string_view(override_host_str),
                                                            false};
  ON_CALL(mock_context, overrideHostToSelect())
      .WillByDefault(testing::Return(absl::make_optional(override_host)));

  envoy_dynamic_module_type_envoy_buffer result_buffer{nullptr, 0};
  size_t size =
      envoy_dynamic_module_callback_lb_context_get_override_host(&mock_context, &result_buffer);
  EXPECT_GT(size, 0);
  std::string host(result_buffer.ptr, result_buffer.length);
  EXPECT_EQ("override.example.com", host);
}

TEST_F(DynamicModuleClusterAbiWithClusterTest, LoadBalancerContextWithConnection) {
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  NiceMock<Network::MockConnection> mock_connection;

  ON_CALL(mock_context, downstreamConnection()).WillByDefault(testing::Return(&mock_connection));
  ON_CALL(mock_connection, id()).WillByDefault(testing::Return(12345));

  uint64_t id =
      envoy_dynamic_module_callback_lb_context_get_downstream_connection_id(&mock_context);
  EXPECT_EQ(12345, id);
}

TEST_F(DynamicModuleClusterAbiWithClusterTest, ClusterGetNameNullResultBuffer) {
  // Test with null result buffer.
  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_get_name(dm_cluster_, nullptr));
}

TEST_F(DynamicModuleClusterAbiWithClusterTest, HostGetAddressNullResultBuffer) {
  Upstream::HostSharedPtr host;
  dm_cluster_->addHost("127.0.0.1:8080", 1, &host);

  EXPECT_EQ(0, envoy_dynamic_module_callback_cluster_host_get_address(host.get(), nullptr));
}

TEST_F(DynamicModuleClusterAbiWithClusterTest, LoadBalancerContextGetHeaderNullResultBuffer) {
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;

  std::string key = "x-test-header";
  envoy_dynamic_module_type_envoy_buffer key_buffer{key.data(), key.size()};
  EXPECT_EQ(
      0, envoy_dynamic_module_callback_lb_context_get_header(&mock_context, key_buffer, nullptr));
}

TEST_F(DynamicModuleClusterAbiWithClusterTest, LoadBalancerContextGetOverrideHostNullResultBuffer) {
  NiceMock<Upstream::MockLoadBalancerContext> mock_context;
  EXPECT_EQ(0, envoy_dynamic_module_callback_lb_context_get_override_host(&mock_context, nullptr));
}

// Test class for cluster manager and thread local cluster callbacks.
class DynamicModuleClusterManagerAbiTest : public testing::Test,
                                           public DynamicModuleClusterFactoryTestBase {
public:
  void SetUp() override {
    // Set up mocks for cluster manager.
    mock_cluster_manager_ = std::make_unique<NiceMock<Upstream::MockClusterManager>>();
    mock_thread_local_cluster_ = std::make_unique<NiceMock<Upstream::MockThreadLocalCluster>>();
    mock_cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();

    // Set up cluster info.
    ON_CALL(*mock_thread_local_cluster_, info()).WillByDefault(testing::Return(mock_cluster_info_));
    ON_CALL(*mock_cluster_info_, name()).WillByDefault(testing::ReturnRef(cluster_name_));

    // Set up priority set - MockThreadLocalCluster has cluster_ with prioritySet().
    ON_CALL(*mock_thread_local_cluster_, prioritySet())
        .WillByDefault(testing::ReturnRef(static_cast<const Upstream::PrioritySet&>(
            mock_thread_local_cluster_->cluster_.prioritySet())));

    // Set up load balancer - MockThreadLocalCluster has lb_ member.
    ON_CALL(*mock_thread_local_cluster_, loadBalancer())
        .WillByDefault(testing::ReturnRef(mock_thread_local_cluster_->lb_));
  }

  NiceMock<Upstream::MockClusterManager>* clusterManager() { return mock_cluster_manager_.get(); }
  NiceMock<Upstream::MockThreadLocalCluster>* threadLocalCluster() {
    return mock_thread_local_cluster_.get();
  }

  std::unique_ptr<NiceMock<Upstream::MockClusterManager>> mock_cluster_manager_;
  std::unique_ptr<NiceMock<Upstream::MockThreadLocalCluster>> mock_thread_local_cluster_;
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> mock_cluster_info_;
  std::string cluster_name_{"mock_cluster"};
};

TEST_F(DynamicModuleClusterManagerAbiTest, GetThreadLocalCluster) {
  ON_CALL(*mock_cluster_manager_, getThreadLocalCluster(testing::_))
      .WillByDefault(testing::Return(mock_thread_local_cluster_.get()));

  std::string name = "test_cluster";
  envoy_dynamic_module_type_envoy_buffer name_buffer{name.data(), name.size()};
  auto* tlc = envoy_dynamic_module_callback_cluster_manager_get_thread_local_cluster(
      clusterManager(), name_buffer);
  EXPECT_EQ(mock_thread_local_cluster_.get(), tlc);
}

TEST_F(DynamicModuleClusterManagerAbiTest, GetThreadLocalClusterNotFound) {
  ON_CALL(*mock_cluster_manager_, getThreadLocalCluster(testing::_))
      .WillByDefault(testing::Return(nullptr));

  std::string name = "nonexistent_cluster";
  envoy_dynamic_module_type_envoy_buffer name_buffer{name.data(), name.size()};
  auto* tlc = envoy_dynamic_module_callback_cluster_manager_get_thread_local_cluster(
      clusterManager(), name_buffer);
  EXPECT_EQ(nullptr, tlc);
}

TEST_F(DynamicModuleClusterManagerAbiTest, ThreadLocalClusterGetName) {
  envoy_dynamic_module_type_envoy_buffer result_buffer{nullptr, 0};
  size_t size = envoy_dynamic_module_callback_thread_local_cluster_get_name(threadLocalCluster(),
                                                                            &result_buffer);
  EXPECT_GT(size, 0);
  std::string name(result_buffer.ptr, result_buffer.length);
  EXPECT_EQ("mock_cluster", name);
}

TEST_F(DynamicModuleClusterManagerAbiTest, ThreadLocalClusterGetNameNullResultBuffer) {
  EXPECT_EQ(0, envoy_dynamic_module_callback_thread_local_cluster_get_name(threadLocalCluster(),
                                                                           nullptr));
}

TEST_F(DynamicModuleClusterManagerAbiTest, ThreadLocalClusterHostCount) {
  // The MockPrioritySet already creates an empty host set at priority 0.
  size_t count =
      envoy_dynamic_module_callback_thread_local_cluster_host_count(threadLocalCluster());
  // Empty host set, so count is 0.
  EXPECT_EQ(0, count);
}

TEST_F(DynamicModuleClusterManagerAbiTest, ThreadLocalClusterChooseHost) {
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(mock_thread_local_cluster_->lb_, chooseHost(testing::_))
      .WillByDefault(testing::Invoke([mock_host](Upstream::LoadBalancerContext*) {
        return Upstream::HostSelectionResponse{mock_host};
      }));

  auto* host =
      envoy_dynamic_module_callback_thread_local_cluster_choose_host(threadLocalCluster(), nullptr);
  EXPECT_EQ(mock_host.get(), host);
}

TEST_F(DynamicModuleClusterManagerAbiTest, ThreadLocalClusterChooseHostReturnsNull) {
  ON_CALL(mock_thread_local_cluster_->lb_, chooseHost(testing::_))
      .WillByDefault(testing::Invoke(
          [](Upstream::LoadBalancerContext*) { return Upstream::HostSelectionResponse{nullptr}; }));

  auto* host =
      envoy_dynamic_module_callback_thread_local_cluster_choose_host(threadLocalCluster(), nullptr);
  EXPECT_EQ(nullptr, host);
}

// Additional ABI tests with actual cluster to cover all code paths.
class DynamicModuleClusterAbiFullCoverageTest : public DynamicModuleClusterTest {};

TEST_F(DynamicModuleClusterAbiFullCoverageTest, ClusterPreInitComplete) {
  // Test pre-init complete via ABI with valid cluster.
  envoy_dynamic_module_callback_cluster_pre_init_complete(dm_cluster_);
  // No crash means success - the module's on_cluster_init already called preInitComplete.
}

TEST_F(DynamicModuleClusterAbiFullCoverageTest, HostHealthUnhealthy) {
  Upstream::HostSharedPtr host;
  dm_cluster_->addHost("127.0.0.1:8080", 1, &host);

  // Mark host as unhealthy.
  host->healthFlagSet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);

  auto health = envoy_dynamic_module_callback_cluster_host_get_health(host.get());
  EXPECT_EQ(envoy_dynamic_module_type_host_health_Unhealthy, health);
}

TEST_F(DynamicModuleClusterAbiFullCoverageTest, HostHealthDegraded) {
  Upstream::HostSharedPtr host;
  dm_cluster_->addHost("127.0.0.1:8080", 1, &host);

  // Mark host as degraded.
  host->healthFlagSet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC);

  auto health = envoy_dynamic_module_callback_cluster_host_get_health(host.get());
  EXPECT_EQ(envoy_dynamic_module_type_host_health_Degraded, health);
}

TEST_F(DynamicModuleClusterAbiFullCoverageTest, RemoveHostNotFound) {
  // Try to remove a non-existent host via ABI.
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto error = envoy_dynamic_module_callback_cluster_remove_host(dm_cluster_, mock_host.get());
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_HostNotFound, error);
}

TEST_F(DynamicModuleClusterAbiFullCoverageTest, SetWeightHostNotFound) {
  // Try to set weight on a non-existent host via ABI.
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto error =
      envoy_dynamic_module_callback_cluster_host_set_weight(dm_cluster_, mock_host.get(), 50);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_HostNotFound, error);
}

// Factory test for config creation failure (module loaded but config creation fails).
class DynamicModuleClusterFactoryConfigFailureTest : public testing::Test,
                                                     public DynamicModuleClusterFactoryTestBase {};

TEST_F(DynamicModuleClusterFactoryConfigFailureTest, ConfigCreationFails) {
  // This test verifies that when module config creation fails, the factory propagates the error.
  // We use the no_op module which doesn't have cluster symbols.
  std::string no_op_path = Envoy::Extensions::DynamicModules::testSharedObjectPath("no_op", "c");
  std::string no_op_dir = std::filesystem::path(no_op_path).parent_path().string();
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", no_op_dir, 1);

  const std::string yaml = R"EOF(
      name: dynamic_module_cluster
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cluster_type:
        name: envoy.clusters.dynamic_modules
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
          dynamic_module_config:
            name: no_op
          cluster_name: test
    )EOF";

  auto cluster_config = parseClusterConfig(yaml);
  auto module_config = parseModuleConfig(cluster_config);
  auto factory_context = createFactoryContext();

  DynamicModuleClusterFactory factory;
  auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
      factory, cluster_config, module_config, factory_context);
  EXPECT_FALSE(result.ok());
  // The error should indicate a symbol lookup failure.
  EXPECT_THAT(std::string(result.status().message()), testing::HasSubstr("symbol"));
}

// Factory test for unknown Any type.
TEST_F(DynamicModuleClusterFactoryTest, WithUnknownAnyType) {
  const std::string yaml = R"EOF(
      name: dynamic_module_cluster
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
            "@type": type.googleapis.com/envoy.config.core.v3.Address
            socket_address:
              address: "127.0.0.1"
              port_value: 8080
    )EOF";

  auto cluster_config = parseClusterConfig(yaml);
  auto module_config = parseModuleConfig(cluster_config);
  auto factory_context = createFactoryContext();

  DynamicModuleClusterFactory factory;
  auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
      factory, cluster_config, module_config, factory_context);
  // Should succeed - unknown types use serialized bytes.
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_NE(result->first, nullptr);
}

// Test factory with malformed StringValue.
TEST_F(DynamicModuleClusterFactoryTest, MalformedStringValue) {
  // Create a cluster config with a StringValue type_url but wrong content.
  auto cluster_config = parseClusterConfig(R"EOF(
      name: dynamic_module_cluster
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cluster_type:
        name: envoy.clusters.dynamic_modules
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
          dynamic_module_config:
            name: cluster_no_op
          cluster_name: test
    )EOF");
  auto module_config = parseModuleConfig(cluster_config);

  // Manually set cluster_config with wrong type.
  Protobuf::Any malformed_any;
  malformed_any.set_type_url("type.googleapis.com/google.protobuf.StringValue");
  // Set invalid protobuf bytes that can't be parsed as StringValue.
  malformed_any.set_value("\xff\xff\xff\xff\xff\xff\xff\xff");
  *module_config.mutable_cluster_config() = malformed_any;

  auto factory_context = createFactoryContext();
  DynamicModuleClusterFactory factory;
  auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
      factory, cluster_config, module_config, factory_context);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(std::string(result.status().message()),
              testing::HasSubstr("Failed to unpack StringValue"));
}

// Test factory with malformed BytesValue.
TEST_F(DynamicModuleClusterFactoryTest, MalformedBytesValue) {
  auto cluster_config = parseClusterConfig(R"EOF(
      name: dynamic_module_cluster
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cluster_type:
        name: envoy.clusters.dynamic_modules
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
          dynamic_module_config:
            name: cluster_no_op
          cluster_name: test
    )EOF");
  auto module_config = parseModuleConfig(cluster_config);

  // Manually set cluster_config with wrong type.
  Protobuf::Any malformed_any;
  malformed_any.set_type_url("type.googleapis.com/google.protobuf.BytesValue");
  malformed_any.set_value("\xff\xff\xff\xff\xff\xff\xff\xff");
  *module_config.mutable_cluster_config() = malformed_any;

  auto factory_context = createFactoryContext();
  DynamicModuleClusterFactory factory;
  auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
      factory, cluster_config, module_config, factory_context);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(std::string(result.status().message()),
              testing::HasSubstr("Failed to unpack BytesValue"));
}

// Test factory with malformed Struct.
TEST_F(DynamicModuleClusterFactoryTest, MalformedStruct) {
  auto cluster_config = parseClusterConfig(R"EOF(
      name: dynamic_module_cluster
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cluster_type:
        name: envoy.clusters.dynamic_modules
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
          dynamic_module_config:
            name: cluster_no_op
          cluster_name: test
    )EOF");
  auto module_config = parseModuleConfig(cluster_config);

  // Manually set cluster_config with wrong type.
  Protobuf::Any malformed_any;
  malformed_any.set_type_url("type.googleapis.com/google.protobuf.Struct");
  malformed_any.set_value("\xff\xff\xff\xff\xff\xff\xff\xff");
  *module_config.mutable_cluster_config() = malformed_any;

  auto factory_context = createFactoryContext();
  DynamicModuleClusterFactory factory;
  auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
      factory, cluster_config, module_config, factory_context);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(std::string(result.status().message()),
              testing::HasSubstr("Failed to unpack Struct"));
}

// Test cleanup is called.
TEST_F(DynamicModuleClusterTest, CleanupDirectCall) {
  // Directly call cleanup via test peer.
  DynamicModuleClusterTestPeer::callCleanup(dm_cluster_);

  // Just verify the cluster is still functional after cleanup.
  Upstream::HostSharedPtr host;
  auto error = dm_cluster_->addHost("127.0.0.1:9999", 1, &host);
  EXPECT_EQ(envoy_dynamic_module_type_cluster_error_Ok, error);
}

// Test health check complete callback with different transitions.
TEST_F(DynamicModuleClusterTest, OnHealthCheckCompleteUnchanged) {
  Upstream::HostSharedPtr host;
  dm_cluster_->addHost("127.0.0.1:8080", 1, &host);

  // Call onHealthCheckComplete with Unchanged transition.
  DynamicModuleClusterTestPeer::callOnHealthCheckComplete(dm_cluster_, host,
                                                          Upstream::HealthTransition::Unchanged);
  // No crash means success - the module's on_host_health_change_ is called.
}

TEST_F(DynamicModuleClusterTest, OnHealthCheckCompleteChanged) {
  Upstream::HostSharedPtr host;
  dm_cluster_->addHost("127.0.0.1:8080", 1, &host);

  // Call onHealthCheckComplete with Changed transition.
  DynamicModuleClusterTestPeer::callOnHealthCheckComplete(dm_cluster_, host,
                                                          Upstream::HealthTransition::Changed);
}

TEST_F(DynamicModuleClusterTest, OnHealthCheckCompleteChangePending) {
  Upstream::HostSharedPtr host;
  dm_cluster_->addHost("127.0.0.1:8080", 1, &host);

  // Call onHealthCheckComplete with ChangePending transition.
  DynamicModuleClusterTestPeer::callOnHealthCheckComplete(
      dm_cluster_, host, Upstream::HealthTransition::ChangePending);
}

// Test that load balancer returns null when the module returns null.
TEST_F(DynamicModuleClusterTest, LoadBalancerChooseHostReturnsNull) {
  // Add a host to the cluster first.
  Upstream::HostSharedPtr host;
  dm_cluster_->addHost("127.0.0.1:8080", 1, &host);

  // Create load balancer.
  auto lb_factory = lb_->factory();
  Upstream::LoadBalancerParams params{cluster_->prioritySet(), nullptr};
  auto lb = lb_factory->create(params);

  // The no_op module's on_load_balancer_choose_host returns nullptr.
  auto result = lb->chooseHost(nullptr);
  EXPECT_EQ(nullptr, result.host);
}

// Test fixture for testing load balancer that returns a valid host.
class DynamicModuleClusterLbReturnsHostTest : public testing::Test {
protected:
  DynamicModuleClusterLbReturnsHostTest() {
    // Set up the module search path to point to the Rust cluster_test module.
    std::string shared_object_path =
        Envoy::Extensions::DynamicModules::testSharedObjectPath("cluster_test", "rust");
    std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);

    ON_CALL(server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
  }

  envoy::config::cluster::v3::Cluster parseClusterConfig(const std::string& yaml) {
    return Upstream::parseClusterFromV3Yaml(yaml);
  }

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig
  parseModuleConfig(const envoy::config::cluster::v3::Cluster& cluster_config) {
    envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig config;
    THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
        cluster_config.cluster_type().typed_config(), ProtobufMessage::getStrictValidationVisitor(),
        config));
    return config;
  }

  Upstream::ClusterFactoryContextImpl createFactoryContext() {
    return Upstream::ClusterFactoryContextImpl(server_context_, nullptr, nullptr, false);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Stats::TestUtil::TestStore& stats_ = server_context_.store_;
  NiceMock<Random::MockRandomGenerator> random_;
  Api::ApiPtr api_ = Api::createApiForTest(stats_, random_);
};

TEST_F(DynamicModuleClusterLbReturnsHostTest, ChooseHostReturnsValidHost) {
  const std::string yaml = R"EOF(
      name: dynamic_module_cluster
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cluster_type:
        name: envoy.clusters.dynamic_modules
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
          dynamic_module_config:
            name: cluster_test
          cluster_name: lb_returns_host
          dynamic_host_discovery: true
    )EOF";

  auto cluster_config = parseClusterConfig(yaml);
  auto module_config = parseModuleConfig(cluster_config);
  auto factory_context = createFactoryContext();

  DynamicModuleClusterFactory factory;
  auto result = DynamicModuleClusterFactoryTestPeer::createClusterWithConfig(
      factory, cluster_config, module_config, factory_context);
  ASSERT_TRUE(result.ok()) << result.status().message();

  auto cluster = result->first;
  auto& ta_lb = result->second;

  // Initialize the cluster.
  cluster->initialize([] { return absl::OkStatus(); });

  // Get the DynamicModuleCluster to add a host.
  auto* dm_cluster = dynamic_cast<DynamicModuleCluster*>(cluster.get());
  ASSERT_NE(dm_cluster, nullptr);

  // Add a host to the cluster.
  Upstream::HostSharedPtr added_host;
  auto error = dm_cluster->addHost("127.0.0.1:8080", 1, &added_host);
  ASSERT_EQ(envoy_dynamic_module_type_cluster_error_Ok, error);
  ASSERT_NE(added_host, nullptr);

  // Create load balancer.
  auto lb_factory = ta_lb->factory();
  Upstream::LoadBalancerParams params{cluster->prioritySet(), nullptr};
  auto lb = lb_factory->create(params);

  // chooseHost should return the host we added.
  auto host_result = lb->chooseHost(nullptr);
  EXPECT_NE(nullptr, host_result.host);
  EXPECT_EQ(added_host.get(), host_result.host.get());
}

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
