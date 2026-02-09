#include "envoy/extensions/load_balancing_policies/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/extensions/load_balancing_policies/dynamic_modules/config.h"
#include "source/extensions/load_balancing_policies/dynamic_modules/load_balancer.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {
namespace {

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

class DynamicModulesLoadBalancerConfigTest : public testing::Test {
protected:
  DynamicModulesLoadBalancerConfigTest() {
    Envoy::Extensions::DynamicModules::DynamicModulesTestEnvironment::setModulesSearchPath();
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  NiceMock<Upstream::MockClusterInfo> cluster_info_;
  NiceMock<Upstream::MockPrioritySet> priority_set_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  Event::SimulatedTimeSystem time_source_;
};

// =============================================================================
// Config Tests
// =============================================================================

TEST_F(DynamicModulesLoadBalancerConfigTest, LoadConfigSuccess) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  EXPECT_TRUE(lb_config_or_error.ok());
  EXPECT_NE(lb_config_or_error.value(), nullptr);
}

TEST_F(DynamicModulesLoadBalancerConfigTest, LoadConfigModuleNotFound) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("nonexistent_module");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  EXPECT_FALSE(lb_config_or_error.ok());
  EXPECT_THAT(lb_config_or_error.status().message(), testing::HasSubstr("failed to load"));
}

TEST_F(DynamicModulesLoadBalancerConfigTest, LoadConfigModuleConfigNewFails) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_config_new_fail");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  EXPECT_FALSE(lb_config_or_error.ok());
  EXPECT_THAT(lb_config_or_error.status().message(),
              testing::HasSubstr("failed to create load balancer config"));
}

TEST_F(DynamicModulesLoadBalancerConfigTest, LoadConfigModuleMissingSymbol) {
  // Test that loading a module missing a required symbol fails gracefully.
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_no_choose_host");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  EXPECT_FALSE(lb_config_or_error.ok());
  EXPECT_THAT(lb_config_or_error.status().message(),
              testing::HasSubstr("envoy_dynamic_module_on_lb_choose_host"));
}

TEST_F(DynamicModulesLoadBalancerConfigTest, LoadConfigWithStringValueConfig) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  // Set up a StringValue config.
  Protobuf::StringValue string_value;
  string_value.set_value("test_config_value");
  config.mutable_lb_policy_config()->PackFrom(string_value);

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  EXPECT_TRUE(lb_config_or_error.ok());
}

TEST_F(DynamicModulesLoadBalancerConfigTest, LoadConfigWithBytesValueConfig) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  // Set up a BytesValue config.
  Protobuf::BytesValue bytes_value;
  bytes_value.set_value("binary_config_data");
  config.mutable_lb_policy_config()->PackFrom(bytes_value);

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  EXPECT_TRUE(lb_config_or_error.ok());
}

TEST_F(DynamicModulesLoadBalancerConfigTest, LoadConfigWithStructConfig) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  // Set up a Struct config.
  Protobuf::Struct struct_value;
  (*struct_value.mutable_fields())["key"].set_string_value("value");
  config.mutable_lb_policy_config()->PackFrom(struct_value);

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  EXPECT_TRUE(lb_config_or_error.ok());
}

// =============================================================================
// Factory::create Tests
// =============================================================================

TEST_F(DynamicModulesLoadBalancerConfigTest, CreateThreadAwareLoadBalancer) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto thread_aware_lb =
      factory.create(OptRef<const Upstream::LoadBalancerConfig>(*lb_config_or_error.value()),
                     cluster_info_, priority_set_, runtime_, random_, time_source_);
  EXPECT_NE(thread_aware_lb, nullptr);

  // Initialize and get the factory.
  EXPECT_TRUE(thread_aware_lb->initialize().ok());
  auto lb_factory = thread_aware_lb->factory();
  EXPECT_NE(lb_factory, nullptr);
}

// =============================================================================
// Load Balancer Tests
// =============================================================================

class DynamicModulesLoadBalancerTest : public testing::Test {
protected:
  DynamicModulesLoadBalancerTest() {
    Envoy::Extensions::DynamicModules::DynamicModulesTestEnvironment::setModulesSearchPath();
  }

  void SetUp() override {
    ON_CALL(cluster_info_, name()).WillByDefault(ReturnRef(cluster_name_));

    // Set up mock hosts.
    host1_ = std::make_shared<NiceMock<Upstream::MockHost>>();
    host2_ = std::make_shared<NiceMock<Upstream::MockHost>>();
    host3_ = std::make_shared<NiceMock<Upstream::MockHost>>();

    auto addr1 = Network::Utility::parseInternetAddressNoThrow("10.0.0.1", 8080, false);
    auto addr2 = Network::Utility::parseInternetAddressNoThrow("10.0.0.2", 8080, false);
    auto addr3 = Network::Utility::parseInternetAddressNoThrow("10.0.0.3", 8080, false);
    ON_CALL(*host1_, address()).WillByDefault(Return(addr1));
    ON_CALL(*host2_, address()).WillByDefault(Return(addr2));
    ON_CALL(*host3_, address()).WillByDefault(Return(addr3));
    ON_CALL(*host1_, weight()).WillByDefault(Return(1));
    ON_CALL(*host2_, weight()).WillByDefault(Return(2));
    ON_CALL(*host3_, weight()).WillByDefault(Return(3));
    ON_CALL(*host1_, coarseHealth()).WillByDefault(Return(Upstream::Host::Health::Healthy));
    ON_CALL(*host2_, coarseHealth()).WillByDefault(Return(Upstream::Host::Health::Healthy));
    ON_CALL(*host3_, coarseHealth()).WillByDefault(Return(Upstream::Host::Health::Degraded));

    // Get the mock host set from the priority set and configure it.
    auto* mock_host_set = priority_set_.getMockHostSet(0);
    mock_host_set->hosts_ = {host1_, host2_, host3_};
    mock_host_set->healthy_hosts_ = {host1_, host2_};
    mock_host_set->degraded_hosts_ = {host3_};

    ON_CALL(*mock_host_set, hosts()).WillByDefault(ReturnRef(mock_host_set->hosts_));
    ON_CALL(*mock_host_set, healthyHosts()).WillByDefault(ReturnRef(mock_host_set->healthy_hosts_));
    ON_CALL(*mock_host_set, degradedHosts())
        .WillByDefault(ReturnRef(mock_host_set->degraded_hosts_));

    ON_CALL(priority_set_, hostSetsPerPriority())
        .WillByDefault(ReturnRef(priority_set_.host_sets_));
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  NiceMock<Upstream::MockClusterInfo> cluster_info_;
  NiceMock<Upstream::MockPrioritySet> priority_set_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  Event::SimulatedTimeSystem time_source_;
  const std::string cluster_name_{"test_cluster"};

  std::shared_ptr<NiceMock<Upstream::MockHost>> host1_;
  std::shared_ptr<NiceMock<Upstream::MockHost>> host2_;
  std::shared_ptr<NiceMock<Upstream::MockHost>> host3_;
};

TEST_F(DynamicModulesLoadBalancerTest, RoundRobinHostSelection) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto thread_aware_lb =
      factory.create(OptRef<const Upstream::LoadBalancerConfig>(*lb_config_or_error.value()),
                     cluster_info_, priority_set_, runtime_, random_, time_source_);
  ASSERT_NE(thread_aware_lb, nullptr);
  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  // Create a worker LB.
  Upstream::LoadBalancerParams params{priority_set_, nullptr};
  auto lb = thread_aware_lb->factory()->create(params);
  ASSERT_NE(lb, nullptr);

  // Test round-robin selection.
  auto response1 = lb->chooseHost(nullptr);
  EXPECT_EQ(response1.host, host1_);

  auto response2 = lb->chooseHost(nullptr);
  EXPECT_EQ(response2.host, host2_);

  auto response3 = lb->chooseHost(nullptr);
  EXPECT_EQ(response3.host, host1_);
}

TEST_F(DynamicModulesLoadBalancerTest, ChooseHostWithContext) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_callbacks_test");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto thread_aware_lb =
      factory.create(OptRef<const Upstream::LoadBalancerConfig>(*lb_config_or_error.value()),
                     cluster_info_, priority_set_, runtime_, random_, time_source_);
  ASSERT_NE(thread_aware_lb, nullptr);
  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  Upstream::LoadBalancerParams params{priority_set_, nullptr};
  auto lb = thread_aware_lb->factory()->create(params);
  ASSERT_NE(lb, nullptr);

  // Create a mock context with headers.
  NiceMock<Upstream::MockLoadBalancerContext> context;
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {"x-test-header", "test-value"}};
  ON_CALL(context, downstreamHeaders()).WillByDefault(Return(&headers));
  ON_CALL(context, computeHashKey()).WillByDefault(Return(absl::optional<uint64_t>(12345)));

  auto response = lb->chooseHost(&context);
  EXPECT_NE(response.host, nullptr);
}

TEST_F(DynamicModulesLoadBalancerTest, ChooseHostNoHealthyHosts) {
  // Set up with no healthy hosts.
  auto* mock_host_set = priority_set_.getMockHostSet(0);
  mock_host_set->healthy_hosts_.clear();
  ON_CALL(*mock_host_set, healthyHosts()).WillByDefault(ReturnRef(mock_host_set->healthy_hosts_));

  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto thread_aware_lb =
      factory.create(OptRef<const Upstream::LoadBalancerConfig>(*lb_config_or_error.value()),
                     cluster_info_, priority_set_, runtime_, random_, time_source_);
  ASSERT_NE(thread_aware_lb, nullptr);
  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  Upstream::LoadBalancerParams params{priority_set_, nullptr};
  auto lb = thread_aware_lb->factory()->create(params);
  ASSERT_NE(lb, nullptr);

  // Should return nullptr when no healthy hosts.
  auto response = lb->chooseHost(nullptr);
  EXPECT_EQ(response.host, nullptr);
}

TEST_F(DynamicModulesLoadBalancerTest, ChooseHostEmptyHostSets) {
  // Set up with empty host sets.
  priority_set_.host_sets_.clear();
  ON_CALL(priority_set_, hostSetsPerPriority()).WillByDefault(ReturnRef(priority_set_.host_sets_));

  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto thread_aware_lb =
      factory.create(OptRef<const Upstream::LoadBalancerConfig>(*lb_config_or_error.value()),
                     cluster_info_, priority_set_, runtime_, random_, time_source_);
  ASSERT_NE(thread_aware_lb, nullptr);
  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  Upstream::LoadBalancerParams params{priority_set_, nullptr};
  auto lb = thread_aware_lb->factory()->create(params);
  ASSERT_NE(lb, nullptr);

  // Should return nullptr when host sets are empty.
  auto response = lb->chooseHost(nullptr);
  EXPECT_EQ(response.host, nullptr);
}

TEST_F(DynamicModulesLoadBalancerTest, LbNewFails) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_new_fail");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto thread_aware_lb =
      factory.create(OptRef<const Upstream::LoadBalancerConfig>(*lb_config_or_error.value()),
                     cluster_info_, priority_set_, runtime_, random_, time_source_);
  ASSERT_NE(thread_aware_lb, nullptr);
  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  Upstream::LoadBalancerParams params{priority_set_, nullptr};
  auto lb = thread_aware_lb->factory()->create(params);
  ASSERT_NE(lb, nullptr);

  // When in_module_lb_ is null, chooseHost should return nullptr.
  auto response = lb->chooseHost(nullptr);
  EXPECT_EQ(response.host, nullptr);
}

TEST_F(DynamicModulesLoadBalancerTest, ChooseHostInvalidIndex) {
  // Test that when the module returns an invalid (too large) host index, we get nullptr.
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_invalid_host_index");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto thread_aware_lb =
      factory.create(OptRef<const Upstream::LoadBalancerConfig>(*lb_config_or_error.value()),
                     cluster_info_, priority_set_, runtime_, random_, time_source_);
  ASSERT_NE(thread_aware_lb, nullptr);
  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  Upstream::LoadBalancerParams params{priority_set_, nullptr};
  auto lb = thread_aware_lb->factory()->create(params);
  ASSERT_NE(lb, nullptr);

  // Module returns index 9999 which is way beyond the number of hosts.
  // Should log a warning and return nullptr.
  auto response = lb->chooseHost(nullptr);
  EXPECT_EQ(response.host, nullptr);
}

TEST_F(DynamicModulesLoadBalancerTest, PeekAnotherHost) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto thread_aware_lb =
      factory.create(OptRef<const Upstream::LoadBalancerConfig>(*lb_config_or_error.value()),
                     cluster_info_, priority_set_, runtime_, random_, time_source_);
  ASSERT_NE(thread_aware_lb, nullptr);
  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  Upstream::LoadBalancerParams params{priority_set_, nullptr};
  auto lb = thread_aware_lb->factory()->create(params);
  ASSERT_NE(lb, nullptr);

  // peekAnotherHost is not implemented, should return nullptr.
  EXPECT_EQ(lb->peekAnotherHost(nullptr), nullptr);
}

TEST_F(DynamicModulesLoadBalancerTest, LifetimeCallbacks) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto thread_aware_lb =
      factory.create(OptRef<const Upstream::LoadBalancerConfig>(*lb_config_or_error.value()),
                     cluster_info_, priority_set_, runtime_, random_, time_source_);
  ASSERT_NE(thread_aware_lb, nullptr);
  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  Upstream::LoadBalancerParams params{priority_set_, nullptr};
  auto lb = thread_aware_lb->factory()->create(params);
  ASSERT_NE(lb, nullptr);

  // lifetimeCallbacks should return empty optional.
  EXPECT_FALSE(lb->lifetimeCallbacks().has_value());
}

TEST_F(DynamicModulesLoadBalancerTest, SelectExistingConnection) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto thread_aware_lb =
      factory.create(OptRef<const Upstream::LoadBalancerConfig>(*lb_config_or_error.value()),
                     cluster_info_, priority_set_, runtime_, random_, time_source_);
  ASSERT_NE(thread_aware_lb, nullptr);
  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  Upstream::LoadBalancerParams params{priority_set_, nullptr};
  auto lb = thread_aware_lb->factory()->create(params);
  ASSERT_NE(lb, nullptr);

  // selectExistingConnection should return nullopt.
  std::vector<uint8_t> hash_key;
  EXPECT_FALSE(lb->selectExistingConnection(nullptr, *host1_, hash_key).has_value());
}

// =============================================================================
// ABI Callback Tests
// =============================================================================

TEST_F(DynamicModulesLoadBalancerTest, AbiCallbacksWithNullPointers) {
  // Test null pointer handling for ABI callbacks.
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};

  // Test with null lb_envoy_ptr.
  envoy_dynamic_module_callback_lb_get_cluster_name(nullptr, &result);
  EXPECT_EQ(result.ptr, nullptr);
  EXPECT_EQ(result.length, 0);

  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_hosts_count(nullptr, 0), 0);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_healthy_hosts_count(nullptr, 0), 0);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_degraded_hosts_count(nullptr, 0), 0);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_priority_set_size(nullptr), 0);
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_healthy_host_address(nullptr, 0, 0, &result));
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_healthy_host_weight(nullptr, 0, 0), 0);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_health(nullptr, 0, 0),
            envoy_dynamic_module_type_host_health_Unhealthy);

  // Test context callbacks with null.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_compute_hash_key(nullptr, nullptr));
  EXPECT_EQ(envoy_dynamic_module_callback_lb_context_get_downstream_headers_size(nullptr), 0);
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_get_downstream_headers(nullptr, nullptr));
  envoy_dynamic_module_type_module_buffer header_key = {"test-key", 8};
  envoy_dynamic_module_type_envoy_buffer header_result = {nullptr, 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_get_downstream_header(
      nullptr, header_key, &header_result, 0, nullptr));
}

TEST_F(DynamicModulesLoadBalancerTest, AbiCallbacksWithInvalidPriority) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto thread_aware_lb =
      factory.create(OptRef<const Upstream::LoadBalancerConfig>(*lb_config_or_error.value()),
                     cluster_info_, priority_set_, runtime_, random_, time_source_);
  ASSERT_NE(thread_aware_lb, nullptr);
  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  Upstream::LoadBalancerParams params{priority_set_, nullptr};
  auto lb = thread_aware_lb->factory()->create(params);
  ASSERT_NE(lb, nullptr);

  // Cast to get access to the raw pointer for ABI testing.
  auto* lb_ptr = static_cast<DynamicModuleLoadBalancer*>(lb.get());

  // Test with invalid priority (beyond what exists).
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_hosts_count(lb_ptr, 999), 0);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_healthy_hosts_count(lb_ptr, 999), 0);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_degraded_hosts_count(lb_ptr, 999), 0);

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_healthy_host_address(lb_ptr, 999, 0, &result));
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_healthy_host_weight(lb_ptr, 999, 0), 0);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_health(lb_ptr, 999, 0),
            envoy_dynamic_module_type_host_health_Unhealthy);
}

TEST_F(DynamicModulesLoadBalancerTest, AbiCallbacksWithInvalidHostIndex) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto thread_aware_lb =
      factory.create(OptRef<const Upstream::LoadBalancerConfig>(*lb_config_or_error.value()),
                     cluster_info_, priority_set_, runtime_, random_, time_source_);
  ASSERT_NE(thread_aware_lb, nullptr);
  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  Upstream::LoadBalancerParams params{priority_set_, nullptr};
  auto lb = thread_aware_lb->factory()->create(params);
  ASSERT_NE(lb, nullptr);

  auto* lb_ptr = static_cast<DynamicModuleLoadBalancer*>(lb.get());

  // Test with invalid host index.
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_healthy_host_address(lb_ptr, 0, 999, &result));
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_healthy_host_weight(lb_ptr, 0, 999), 0);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_health(lb_ptr, 0, 999),
            envoy_dynamic_module_type_host_health_Unhealthy);
}

TEST_F(DynamicModulesLoadBalancerTest, AbiCallbacksSuccessfulCases) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto thread_aware_lb =
      factory.create(OptRef<const Upstream::LoadBalancerConfig>(*lb_config_or_error.value()),
                     cluster_info_, priority_set_, runtime_, random_, time_source_);
  ASSERT_NE(thread_aware_lb, nullptr);
  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  Upstream::LoadBalancerParams params{priority_set_, nullptr};
  auto lb = thread_aware_lb->factory()->create(params);
  ASSERT_NE(lb, nullptr);

  auto* lb_ptr = static_cast<DynamicModuleLoadBalancer*>(lb.get());

  // Test cluster name.
  envoy_dynamic_module_type_envoy_buffer cluster_name_result = {nullptr, 0};
  envoy_dynamic_module_callback_lb_get_cluster_name(lb_ptr, &cluster_name_result);
  EXPECT_NE(cluster_name_result.ptr, nullptr);
  EXPECT_EQ(absl::string_view(cluster_name_result.ptr, cluster_name_result.length), "test_cluster");

  // Test successful callbacks.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_hosts_count(lb_ptr, 0), 3);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_healthy_hosts_count(lb_ptr, 0), 2);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_degraded_hosts_count(lb_ptr, 0), 1);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_priority_set_size(lb_ptr), 1);

  // Test host address.
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_get_healthy_host_address(lb_ptr, 0, 0, &result));
  EXPECT_NE(result.ptr, nullptr);
  EXPECT_GT(result.length, 0);

  // Test host weight.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_healthy_host_weight(lb_ptr, 0, 0), 1);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_healthy_host_weight(lb_ptr, 0, 1), 2);

  // Test host health.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_health(lb_ptr, 0, 0),
            envoy_dynamic_module_type_host_health_Healthy);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_health(lb_ptr, 0, 1),
            envoy_dynamic_module_type_host_health_Healthy);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_health(lb_ptr, 0, 2),
            envoy_dynamic_module_type_host_health_Degraded);
}

TEST_F(DynamicModulesLoadBalancerTest, ContextCallbacksSuccessfulCases) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto thread_aware_lb =
      factory.create(OptRef<const Upstream::LoadBalancerConfig>(*lb_config_or_error.value()),
                     cluster_info_, priority_set_, runtime_, random_, time_source_);
  ASSERT_NE(thread_aware_lb, nullptr);
  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  Upstream::LoadBalancerParams params{priority_set_, nullptr};
  auto lb = thread_aware_lb->factory()->create(params);
  ASSERT_NE(lb, nullptr);

  // Create context with headers.
  NiceMock<Upstream::MockLoadBalancerContext> context;
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/"}, {"x-custom-header", "custom-value"}};
  ON_CALL(context, downstreamHeaders()).WillByDefault(Return(&headers));
  ON_CALL(context, computeHashKey()).WillByDefault(Return(absl::optional<uint64_t>(42)));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  // Test hash key.
  uint64_t hash_out = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_context_compute_hash_key(context_ptr, &hash_out));
  EXPECT_EQ(hash_out, 42);

  // Test headers size.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_context_get_downstream_headers_size(context_ptr), 3);

  // Test get all headers.
  std::vector<envoy_dynamic_module_type_envoy_http_header> all_headers(3);
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_context_get_downstream_headers(context_ptr,
                                                                              all_headers.data()));
  for (size_t i = 0; i < 3; i++) {
    EXPECT_NE(all_headers[i].key_ptr, nullptr);
    EXPECT_GT(all_headers[i].key_length, 0);
    EXPECT_NE(all_headers[i].value_ptr, nullptr);
    EXPECT_GT(all_headers[i].value_length, 0);
  }

  // Test get header by key.
  envoy_dynamic_module_type_module_buffer method_key = {":method", 7};
  envoy_dynamic_module_type_envoy_buffer method_result = {nullptr, 0};
  size_t method_count = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_context_get_downstream_header(
      context_ptr, method_key, &method_result, 0, &method_count));
  EXPECT_EQ(method_count, 1);
  EXPECT_EQ(absl::string_view(method_result.ptr, method_result.length), "GET");

  // Test get header by key with custom header.
  envoy_dynamic_module_type_module_buffer custom_key = {"x-custom-header", 15};
  envoy_dynamic_module_type_envoy_buffer custom_result = {nullptr, 0};
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_context_get_downstream_header(
      context_ptr, custom_key, &custom_result, 0, nullptr));
  EXPECT_EQ(absl::string_view(custom_result.ptr, custom_result.length), "custom-value");

  // Test get header with non-existent key.
  envoy_dynamic_module_type_module_buffer nonexistent_key = {"nonexistent", 11};
  envoy_dynamic_module_type_envoy_buffer nonexistent_result = {nullptr, 0};
  size_t nonexistent_count = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_get_downstream_header(
      context_ptr, nonexistent_key, &nonexistent_result, 0, &nonexistent_count));
  EXPECT_EQ(nonexistent_count, 0);

  // Test get header with out-of-bounds index.
  envoy_dynamic_module_type_envoy_buffer oob_result = {nullptr, 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_get_downstream_header(
      context_ptr, method_key, &oob_result, 999, nullptr));
}

TEST_F(DynamicModulesLoadBalancerTest, ContextCallbacksNoHashKey) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, computeHashKey()).WillByDefault(Return(absl::nullopt));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  uint64_t hash_out = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_compute_hash_key(context_ptr, &hash_out));
}

TEST_F(DynamicModulesLoadBalancerTest, ContextCallbacksNoHeaders) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, downstreamHeaders()).WillByDefault(Return(nullptr));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  EXPECT_EQ(envoy_dynamic_module_callback_lb_context_get_downstream_headers_size(context_ptr), 0);

  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_context_get_downstream_headers(context_ptr, nullptr));

  envoy_dynamic_module_type_module_buffer key = {":method", 7};
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  size_t count = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_get_downstream_header(context_ptr, key,
                                                                              &result, 0, &count));
  EXPECT_EQ(count, 0);
}

TEST_F(DynamicModulesLoadBalancerTest, ContextCallbacksHeaderOutOfBounds) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}};
  ON_CALL(context, downstreamHeaders()).WillByDefault(Return(&headers));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  // Test getting a header value at an out-of-bounds index for a key that exists.
  envoy_dynamic_module_type_module_buffer key = {":method", 7};
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_get_downstream_header(
      context_ptr, key, &result, 999, nullptr));
}

} // namespace
} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
