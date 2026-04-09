#include "envoy/extensions/load_balancing_policies/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/upstream/upstream_impl.h"
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
    ON_CALL(*host1_, locality()).WillByDefault(ReturnRef(default_locality_));
    ON_CALL(*host2_, locality()).WillByDefault(ReturnRef(default_locality_));
    ON_CALL(*host3_, locality()).WillByDefault(ReturnRef(default_locality_));

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
  envoy::config::core::v3::Locality default_locality_;

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

  // Module returns priority=0, index=9999 which is way beyond the number of hosts. Should log a
  // warning and return nullptr.
  auto response = lb->chooseHost(nullptr);
  EXPECT_EQ(response.host, nullptr);
}

TEST_F(DynamicModulesLoadBalancerTest, ChooseHostInvalidPriority) {
  // Test that when the module returns an invalid priority, we get nullptr.
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_invalid_priority");
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

  // Module returns priority=99, index=0 which is an invalid priority. Should log a warning and
  // return nullptr.
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

  // Test new all-hosts callbacks with null lb_envoy_ptr.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_address(nullptr, 0, 0, &result));
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_weight(nullptr, 0, 0), 0);
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_get_host_locality(nullptr, 0, 0, nullptr, nullptr, nullptr));

  // Test context callbacks with null.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_compute_hash_key(nullptr, nullptr));
  EXPECT_EQ(envoy_dynamic_module_callback_lb_context_get_downstream_headers_size(nullptr), 0);
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_get_downstream_headers(nullptr, nullptr));
  envoy_dynamic_module_type_module_buffer header_key = {"test-key", 8};
  envoy_dynamic_module_type_envoy_buffer header_result = {nullptr, 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_get_downstream_header(
      nullptr, header_key, &header_result, 0, nullptr));

  // Test retry-awareness context callbacks with null.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_context_get_host_selection_retry_count(nullptr), 0);
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_context_should_select_another_host(nullptr, nullptr, 0, 0));

  // Test override host context callback with null.
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_context_get_override_host(nullptr, nullptr, nullptr));

  // Test member update host address callback with null.
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_get_member_update_host_address(nullptr, 0, true, &result));

  // Test host stat callback with null.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                nullptr, 0, 0, envoy_dynamic_module_type_host_stat_RqTotal),
            0);
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

  // Test new all-hosts callbacks with invalid priority.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_address(lb_ptr, 999, 0, &result));
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_weight(lb_ptr, 999, 0), 0);
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_locality(lb_ptr, 999, 0, nullptr, nullptr,
                                                                  nullptr));

  // Test host stat with invalid priority.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                lb_ptr, 999, 0, envoy_dynamic_module_type_host_stat_RqTotal),
            0);
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

  // Test new all-hosts callbacks with invalid host index.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_address(lb_ptr, 0, 999, &result));
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_weight(lb_ptr, 0, 999), 0);
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_locality(lb_ptr, 0, 999, nullptr, nullptr,
                                                                  nullptr));

  // Test host stat with invalid host index.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                lb_ptr, 0, 999, envoy_dynamic_module_type_host_stat_RqTotal),
            0);
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

  // Test all-hosts address callback.
  envoy_dynamic_module_type_envoy_buffer host_addr_result = {nullptr, 0};
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_get_host_address(lb_ptr, 0, 0, &host_addr_result));
  EXPECT_NE(host_addr_result.ptr, nullptr);
  EXPECT_GT(host_addr_result.length, 0);

  // Test all-hosts weight callback.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_weight(lb_ptr, 0, 0), 1);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_weight(lb_ptr, 0, 1), 2);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_weight(lb_ptr, 0, 2), 3);

  // Test host stat callback (gauges).
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                lb_ptr, 0, 0, envoy_dynamic_module_type_host_stat_RqActive),
            0);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                lb_ptr, 0, 0, envoy_dynamic_module_type_host_stat_CxActive),
            0);

  // Test locality callback.
  envoy_dynamic_module_type_envoy_buffer region_result = {nullptr, 0};
  envoy_dynamic_module_type_envoy_buffer zone_result = {nullptr, 0};
  envoy_dynamic_module_type_envoy_buffer sub_zone_result = {nullptr, 0};
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_get_host_locality(lb_ptr, 0, 0, &region_result,
                                                                 &zone_result, &sub_zone_result));
}

TEST_F(DynamicModulesLoadBalancerTest, AbiCallbacksHostStatsAndLocality) {
  // Set up locality on hosts.
  envoy::config::core::v3::Locality locality1;
  locality1.set_region("us-east");
  locality1.set_zone("us-east-1a");
  locality1.set_sub_zone("subnet-1");
  ON_CALL(*host1_, locality()).WillByDefault(ReturnRef(locality1));

  // Set active request/connection stats on hosts.
  host1_->stats().rq_active_.set(5);
  host1_->stats().cx_active_.set(3);
  host2_->stats().rq_active_.set(10);
  host2_->stats().cx_active_.set(7);

  // Set counter stats on hosts.
  host1_->stats().cx_connect_fail_.inc();
  host1_->stats().cx_connect_fail_.inc();
  host1_->stats().cx_total_.add(100);
  host1_->stats().rq_error_.add(3);
  host1_->stats().rq_success_.add(42);
  host1_->stats().rq_timeout_.inc();
  host1_->stats().rq_total_.add(46);

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

  // Verify active requests via get_host_stat.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                lb_ptr, 0, 0, envoy_dynamic_module_type_host_stat_RqActive),
            5);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                lb_ptr, 0, 1, envoy_dynamic_module_type_host_stat_RqActive),
            10);

  // Verify active connections via get_host_stat.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                lb_ptr, 0, 0, envoy_dynamic_module_type_host_stat_CxActive),
            3);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                lb_ptr, 0, 1, envoy_dynamic_module_type_host_stat_CxActive),
            7);

  // Verify locality.
  envoy_dynamic_module_type_envoy_buffer region = {nullptr, 0};
  envoy_dynamic_module_type_envoy_buffer zone = {nullptr, 0};
  envoy_dynamic_module_type_envoy_buffer sub_zone = {nullptr, 0};
  EXPECT_TRUE(
      envoy_dynamic_module_callback_lb_get_host_locality(lb_ptr, 0, 0, &region, &zone, &sub_zone));
  EXPECT_EQ(absl::string_view(region.ptr, region.length), "us-east");
  EXPECT_EQ(absl::string_view(zone.ptr, zone.length), "us-east-1a");
  EXPECT_EQ(absl::string_view(sub_zone.ptr, sub_zone.length), "subnet-1");

  // Test locality with partial null output buffers.
  EXPECT_TRUE(
      envoy_dynamic_module_callback_lb_get_host_locality(lb_ptr, 0, 0, &region, nullptr, nullptr));
  EXPECT_EQ(absl::string_view(region.ptr, region.length), "us-east");

  // Verify host stats (counters).
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                lb_ptr, 0, 0, envoy_dynamic_module_type_host_stat_CxConnectFail),
            2);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                lb_ptr, 0, 0, envoy_dynamic_module_type_host_stat_CxTotal),
            100);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                lb_ptr, 0, 0, envoy_dynamic_module_type_host_stat_RqError),
            3);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                lb_ptr, 0, 0, envoy_dynamic_module_type_host_stat_RqSuccess),
            42);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                lb_ptr, 0, 0, envoy_dynamic_module_type_host_stat_RqTimeout),
            1);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                lb_ptr, 0, 0, envoy_dynamic_module_type_host_stat_RqTotal),
            46);

  // Verify host2 stats are 0 (not set).
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_host_stat(
                lb_ptr, 0, 1, envoy_dynamic_module_type_host_stat_RqTotal),
            0);
}

TEST_F(DynamicModulesLoadBalancerTest, HostHealthByAddressSuccess) {
  // Set up a cross-priority host map containing the test hosts.
  auto host_map = std::make_shared<Upstream::HostMap>();
  host_map->insert({"10.0.0.1:8080", host1_});
  host_map->insert({"10.0.0.2:8080", host2_});
  host_map->insert({"10.0.0.3:8080", host3_});
  ON_CALL(priority_set_, crossPriorityHostMap()).WillByDefault(Return(host_map));

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

  // Lookup healthy host by address.
  envoy_dynamic_module_type_host_health health = envoy_dynamic_module_type_host_health_Unhealthy;
  envoy_dynamic_module_type_module_buffer addr1 = {"10.0.0.1:8080", 13};
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_get_host_health_by_address(lb_ptr, addr1, &health));
  EXPECT_EQ(health, envoy_dynamic_module_type_host_health_Healthy);

  // Lookup another healthy host.
  envoy_dynamic_module_type_module_buffer addr2 = {"10.0.0.2:8080", 13};
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_get_host_health_by_address(lb_ptr, addr2, &health));
  EXPECT_EQ(health, envoy_dynamic_module_type_host_health_Healthy);

  // Lookup degraded host.
  envoy_dynamic_module_type_module_buffer addr3 = {"10.0.0.3:8080", 13};
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_get_host_health_by_address(lb_ptr, addr3, &health));
  EXPECT_EQ(health, envoy_dynamic_module_type_host_health_Degraded);

  // Lookup non-existent address.
  envoy_dynamic_module_type_module_buffer bad_addr = {"1.2.3.4:9999", 12};
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_get_host_health_by_address(lb_ptr, bad_addr, &health));
}

TEST_F(DynamicModulesLoadBalancerTest, HostHealthByAddressNullInputs) {
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

  // Null lb_envoy_ptr.
  envoy_dynamic_module_type_host_health health = envoy_dynamic_module_type_host_health_Healthy;
  envoy_dynamic_module_type_module_buffer addr = {"10.0.0.1:8080", 13};
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_health_by_address(nullptr, addr, &health));
  EXPECT_EQ(health, envoy_dynamic_module_type_host_health_Unhealthy);

  // Null result pointer.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_health_by_address(lb_ptr, addr, nullptr));

  // Null address pointer.
  envoy_dynamic_module_type_module_buffer null_addr = {nullptr, 0};
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_get_host_health_by_address(lb_ptr, null_addr, &health));

  // Null host map (default mock returns nullptr).
  envoy_dynamic_module_type_module_buffer valid_addr = {"10.0.0.1:8080", 13};
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_get_host_health_by_address(lb_ptr, valid_addr, &health));
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

// =============================================================================
// Retry Awareness Tests
// =============================================================================

TEST_F(DynamicModulesLoadBalancerTest, HostSelectionRetryCount) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, hostSelectionRetryCount()).WillByDefault(Return(3));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  EXPECT_EQ(envoy_dynamic_module_callback_lb_context_get_host_selection_retry_count(context_ptr),
            3);
}

TEST_F(DynamicModulesLoadBalancerTest, HostSelectionRetryCountZero) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, hostSelectionRetryCount()).WillByDefault(Return(0));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  EXPECT_EQ(envoy_dynamic_module_callback_lb_context_get_host_selection_retry_count(context_ptr),
            0);
}

TEST_F(DynamicModulesLoadBalancerTest, ShouldSelectAnotherHostAccepted) {
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

  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, shouldSelectAnotherHost(_)).WillByDefault(Return(false));
  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  // Host should be accepted (shouldSelectAnotherHost returns false).
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_should_select_another_host(
      lb_ptr, context_ptr, 0, 0));
}

TEST_F(DynamicModulesLoadBalancerTest, ShouldSelectAnotherHostRejected) {
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

  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, shouldSelectAnotherHost(_)).WillByDefault(Return(true));
  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  // Host should be rejected (shouldSelectAnotherHost returns true).
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_context_should_select_another_host(
      lb_ptr, context_ptr, 0, 0));
}

TEST_F(DynamicModulesLoadBalancerTest, ShouldSelectAnotherHostInvalidPriority) {
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

  NiceMock<Upstream::MockLoadBalancerContext> context;
  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  // Invalid priority returns false.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_should_select_another_host(
      lb_ptr, context_ptr, 999, 0));

  // Invalid host index returns false.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_context_should_select_another_host(
      lb_ptr, context_ptr, 0, 999));
}

// =============================================================================
// Override Host Selection Tests
// =============================================================================

TEST_F(DynamicModulesLoadBalancerTest, OverrideHostPresent) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  Upstream::LoadBalancerContext::OverrideHost override_host{"10.0.0.1:8080", true};
  ON_CALL(context, overrideHostToSelect())
      .WillByDefault(
          Return(OptRef<const Upstream::LoadBalancerContext::OverrideHost>(override_host)));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  envoy_dynamic_module_type_envoy_buffer address = {nullptr, 0};
  bool strict = false;
  EXPECT_TRUE(
      envoy_dynamic_module_callback_lb_context_get_override_host(context_ptr, &address, &strict));
  EXPECT_EQ(absl::string_view(address.ptr, address.length), "10.0.0.1:8080");
  EXPECT_TRUE(strict);
}

TEST_F(DynamicModulesLoadBalancerTest, OverrideHostPresentNonStrict) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  Upstream::LoadBalancerContext::OverrideHost override_host{"10.0.0.2:9090", false};
  ON_CALL(context, overrideHostToSelect())
      .WillByDefault(
          Return(OptRef<const Upstream::LoadBalancerContext::OverrideHost>(override_host)));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  envoy_dynamic_module_type_envoy_buffer address = {nullptr, 0};
  bool strict = true;
  EXPECT_TRUE(
      envoy_dynamic_module_callback_lb_context_get_override_host(context_ptr, &address, &strict));
  EXPECT_EQ(absl::string_view(address.ptr, address.length), "10.0.0.2:9090");
  EXPECT_FALSE(strict);
}

TEST_F(DynamicModulesLoadBalancerTest, OverrideHostNotSet) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  ON_CALL(context, overrideHostToSelect())
      .WillByDefault(Return(OptRef<const Upstream::LoadBalancerContext::OverrideHost>()));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  envoy_dynamic_module_type_envoy_buffer address = {nullptr, 0};
  bool strict = false;
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_context_get_override_host(context_ptr, &address, &strict));
}

TEST_F(DynamicModulesLoadBalancerTest, OverrideHostNullOutputs) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  Upstream::LoadBalancerContext::OverrideHost override_host_null_test{"10.0.0.1:8080", true};
  ON_CALL(context, overrideHostToSelect())
      .WillByDefault(Return(
          OptRef<const Upstream::LoadBalancerContext::OverrideHost>(override_host_null_test)));

  auto* context_ptr = static_cast<Upstream::LoadBalancerContext*>(&context);

  // Null address output.
  bool strict = false;
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_context_get_override_host(context_ptr, nullptr, &strict));

  // Null strict output.
  envoy_dynamic_module_type_envoy_buffer address = {nullptr, 0};
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_context_get_override_host(context_ptr, &address, nullptr));
}

// =============================================================================
// Per-Host Data Storage Tests
// =============================================================================

TEST_F(DynamicModulesLoadBalancerTest, PerHostDataSetAndGet) {
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

  // Set data on host 0.
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_set_host_data(lb_ptr, 0, 0, 42));

  // Get data from host 0.
  uintptr_t data = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_get_host_data(lb_ptr, 0, 0, &data));
  EXPECT_EQ(data, 42);

  // Get data from host with no data stored.
  uintptr_t data2 = 99;
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_get_host_data(lb_ptr, 0, 1, &data2));
  EXPECT_EQ(data2, 0);

  // Clear data by setting to 0.
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_set_host_data(lb_ptr, 0, 0, 0));
  uintptr_t data3 = 99;
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_get_host_data(lb_ptr, 0, 0, &data3));
  EXPECT_EQ(data3, 0);

  // Invalid priority.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_set_host_data(lb_ptr, 999, 0, 42));
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_data(lb_ptr, 999, 0, &data));

  // Invalid host index.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_set_host_data(lb_ptr, 0, 999, 42));
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_data(lb_ptr, 0, 999, &data));

  // Null pointer handling.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_set_host_data(nullptr, 0, 0, 42));
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_data(nullptr, 0, 0, &data));
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_data(lb_ptr, 0, 0, nullptr));
}

// =============================================================================
// Host Metadata Tests
// =============================================================================

TEST_F(DynamicModulesLoadBalancerTest, HostMetadataTypedAccessSuccess) {
  // Set up metadata on host1 with string, number, and bool values.
  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  auto& filter_metadata = (*metadata->mutable_filter_metadata())["envoy.lb"];
  (*filter_metadata.mutable_fields())["version"].set_string_value("v1.0");
  (*filter_metadata.mutable_fields())["weight_factor"].set_number_value(1.5);
  (*filter_metadata.mutable_fields())["enabled"].set_bool_value(true);
  ON_CALL(*host1_, metadata()).WillByDefault(Return(metadata));

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

  envoy_dynamic_module_type_module_buffer filter_name = {"envoy.lb", 8};

  // Test string value lookup.
  envoy_dynamic_module_type_module_buffer key = {"version", 7};
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_get_host_metadata_string(lb_ptr, 0, 0, filter_name,
                                                                        key, &result));
  EXPECT_NE(result.ptr, nullptr);
  EXPECT_EQ(absl::string_view(result.ptr, result.length), "v1.0");

  // Test number value lookup.
  envoy_dynamic_module_type_module_buffer num_key = {"weight_factor", 13};
  double num_result = 0.0;
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_get_host_metadata_number(lb_ptr, 0, 0, filter_name,
                                                                        num_key, &num_result));
  EXPECT_DOUBLE_EQ(num_result, 1.5);

  // Test bool value lookup.
  envoy_dynamic_module_type_module_buffer bool_key = {"enabled", 7};
  bool bool_result = false;
  EXPECT_TRUE(envoy_dynamic_module_callback_lb_get_host_metadata_bool(lb_ptr, 0, 0, filter_name,
                                                                      bool_key, &bool_result));
  EXPECT_TRUE(bool_result);

  // Test type mismatch: string key with number accessor returns false.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_metadata_number(lb_ptr, 0, 0, filter_name,
                                                                         key, &num_result));

  // Test type mismatch: number key with string accessor returns false.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_metadata_string(lb_ptr, 0, 0, filter_name,
                                                                         num_key, &result));
}

TEST_F(DynamicModulesLoadBalancerTest, HostMetadataNotFound) {
  // Set up metadata without the requested key.
  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  auto& filter_metadata = (*metadata->mutable_filter_metadata())["envoy.lb"];
  (*filter_metadata.mutable_fields())["other_key"].set_string_value("other_value");
  ON_CALL(*host1_, metadata()).WillByDefault(Return(metadata));

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

  envoy_dynamic_module_type_module_buffer filter_name = {"envoy.lb", 8};
  envoy_dynamic_module_type_module_buffer key = {"version", 7};
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};

  // Non-existent filter name.
  envoy_dynamic_module_type_module_buffer bad_filter = {"nonexistent", 11};
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_metadata_string(lb_ptr, 0, 0, bad_filter,
                                                                         key, &result));

  // Non-existent key.
  envoy_dynamic_module_type_module_buffer bad_key = {"nonexistent", 11};
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_metadata_string(lb_ptr, 0, 0, filter_name,
                                                                         bad_key, &result));

  // Null metadata on host.
  ON_CALL(*host1_, metadata()).WillByDefault(Return(nullptr));
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_metadata_string(lb_ptr, 0, 0, filter_name,
                                                                         key, &result));

  // Null pointer handling.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_metadata_string(nullptr, 0, 0, filter_name,
                                                                         key, &result));
  double num = 0.0;
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_metadata_number(nullptr, 0, 0, filter_name,
                                                                         key, &num));
  bool b = false;
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_get_host_metadata_bool(nullptr, 0, 0, filter_name, key, &b));

  // Invalid priority/index.
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_metadata_string(
      lb_ptr, 999, 0, filter_name, key, &result));
  EXPECT_FALSE(envoy_dynamic_module_callback_lb_get_host_metadata_string(
      lb_ptr, 0, 999, filter_name, key, &result));
}

// =============================================================================
// Locality Hosts & Weights Tests
// =============================================================================

TEST_F(DynamicModulesLoadBalancerTest, LocalityCallbacksSuccess) {
  // Set up hosts per locality on the host set.
  auto* mock_host_set = priority_set_.getMockHostSet(0);

  // Create locality buckets with hosts.
  Upstream::HostVector locality0_hosts = {host1_};
  Upstream::HostVector locality1_hosts = {host2_};
  std::vector<Upstream::HostVector> hosts_per_locality = {locality0_hosts, locality1_hosts};

  auto hosts_per_locality_ptr =
      std::make_shared<Upstream::HostsPerLocalityImpl>(std::move(hosts_per_locality), false);
  ON_CALL(*mock_host_set, healthyHostsPerLocality())
      .WillByDefault(ReturnRef(*hosts_per_locality_ptr));

  // Set up locality weights.
  auto locality_weights =
      std::make_shared<Upstream::LocalityWeights>(Upstream::LocalityWeights{70, 30});
  ON_CALL(*mock_host_set, localityWeights()).WillByDefault(Return(locality_weights));

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

  // Test locality count.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_locality_count(lb_ptr, 0), 2);

  // Test hosts per locality.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_locality_host_count(lb_ptr, 0, 0), 1);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_locality_host_count(lb_ptr, 0, 1), 1);

  // Test host address in locality.
  envoy_dynamic_module_type_envoy_buffer addr_result = {nullptr, 0};
  EXPECT_TRUE(
      envoy_dynamic_module_callback_lb_get_locality_host_address(lb_ptr, 0, 0, 0, &addr_result));
  EXPECT_NE(addr_result.ptr, nullptr);
  EXPECT_GT(addr_result.length, 0);

  // Test invalid host index within a valid locality.
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_get_locality_host_address(lb_ptr, 0, 0, 999, &addr_result));

  // Test locality weights.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_locality_weight(lb_ptr, 0, 0), 70);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_locality_weight(lb_ptr, 0, 1), 30);
}

TEST_F(DynamicModulesLoadBalancerTest, LocalityCallbacksEdgeCases) {
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

  // Null pointer handling.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_locality_count(nullptr, 0), 0);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_locality_host_count(nullptr, 0, 0), 0);
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_get_locality_host_address(nullptr, 0, 0, 0, &result));
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_locality_weight(nullptr, 0, 0), 0);

  // Invalid priority.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_locality_count(lb_ptr, 999), 0);
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_locality_host_count(lb_ptr, 999, 0), 0);
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_get_locality_host_address(lb_ptr, 999, 0, 0, &result));
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_locality_weight(lb_ptr, 999, 0), 0);

  // Invalid locality index.
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_locality_host_count(lb_ptr, 0, 999), 0);
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_get_locality_host_address(lb_ptr, 0, 999, 0, &result));
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_locality_weight(lb_ptr, 0, 999), 0);

  // Null locality weights.
  auto* mock_host_set = priority_set_.getMockHostSet(0);
  ON_CALL(*mock_host_set, localityWeights()).WillByDefault(Return(nullptr));
  EXPECT_EQ(envoy_dynamic_module_callback_lb_get_locality_weight(lb_ptr, 0, 0), 0);
}

// =============================================================================
// Callbacks Test Module Integration Test
// =============================================================================

TEST_F(DynamicModulesLoadBalancerTest, CallbacksTestModuleExercisesNewCallbacks) {
  // Set up metadata on host1 so the callbacks_test module can test metadata access.
  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  auto& filter_metadata = (*metadata->mutable_filter_metadata())["envoy.lb"];
  (*filter_metadata.mutable_fields())["version"].set_string_value("v1.0");
  ON_CALL(*host1_, metadata()).WillByDefault(Return(metadata));

  // Set up locality hosts for the callbacks_test module.
  auto* mock_host_set = priority_set_.getMockHostSet(0);
  Upstream::HostVector locality0_hosts = {host1_, host2_};
  std::vector<Upstream::HostVector> hosts_per_locality = {locality0_hosts};
  auto hosts_per_locality_ptr =
      std::make_shared<Upstream::HostsPerLocalityImpl>(std::move(hosts_per_locality), false);
  ON_CALL(*mock_host_set, healthyHostsPerLocality())
      .WillByDefault(ReturnRef(*hosts_per_locality_ptr));

  auto locality_weights =
      std::make_shared<Upstream::LocalityWeights>(Upstream::LocalityWeights{100});
  ON_CALL(*mock_host_set, localityWeights()).WillByDefault(Return(locality_weights));

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

  // Exercise the module with context.
  NiceMock<Upstream::MockLoadBalancerContext> context;
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {"x-test-header", "test-value"}};
  ON_CALL(context, downstreamHeaders()).WillByDefault(Return(&headers));
  ON_CALL(context, computeHashKey()).WillByDefault(Return(absl::optional<uint64_t>(12345)));

  auto response = lb->chooseHost(&context);
  EXPECT_NE(response.host, nullptr);
}

// =============================================================================
// Host Membership Update Tests
// =============================================================================

TEST_F(DynamicModulesLoadBalancerTest, HostMembershipUpdateNotifiesModule) {
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

  // Trigger a host membership update with hosts added and removed.
  auto new_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto new_addr = Network::Utility::parseInternetAddressNoThrow("10.0.0.4", 8080, false);
  ON_CALL(*new_host, address()).WillByDefault(Return(new_addr));

  Upstream::HostVector hosts_added = {new_host};
  Upstream::HostVector hosts_removed = {host3_};

  priority_set_.runUpdateCallbacks(0, hosts_added, hosts_removed);

  // Verify the LB still works after the update.
  auto response = lb->chooseHost(nullptr);
  EXPECT_NE(response.host, nullptr);
}

TEST_F(DynamicModulesLoadBalancerTest, HostMembershipUpdateEmptyVectors) {
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

  // Trigger an update with empty vectors (no hosts added or removed).
  Upstream::HostVector empty_added;
  Upstream::HostVector empty_removed;
  priority_set_.runUpdateCallbacks(0, empty_added, empty_removed);

  // Verify the LB still works after the no-op update.
  auto response = lb->chooseHost(nullptr);
  EXPECT_NE(response.host, nullptr);
}

TEST_F(DynamicModulesLoadBalancerTest, HostMembershipUpdateCallbackAddress) {
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

  // Verify host pointers are null when not in callback.
  EXPECT_EQ(lb_ptr->hostsAdded(), nullptr);
  EXPECT_EQ(lb_ptr->hostsRemoved(), nullptr);

  // Verify the callback to get addresses returns false when not in an update callback.
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_get_member_update_host_address(lb_ptr, 0, true, &result));
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_get_member_update_host_address(lb_ptr, 0, false, &result));

  // Verify null pointer handling.
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_get_member_update_host_address(nullptr, 0, true, &result));
  EXPECT_FALSE(
      envoy_dynamic_module_callback_lb_get_member_update_host_address(lb_ptr, 0, true, nullptr));
}

TEST_F(DynamicModulesLoadBalancerTest, LbNewFailDoesNotRegisterCallback) {
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

  // Triggering an update should be safe even when on_lb_new returned null.
  Upstream::HostVector empty_added;
  Upstream::HostVector empty_removed;
  priority_set_.runUpdateCallbacks(0, empty_added, empty_removed);

  // chooseHost should return null since the module failed to initialize.
  auto response = lb->chooseHost(nullptr);
  EXPECT_EQ(response.host, nullptr);
}

// =============================================================================
// Metrics Tests
// =============================================================================

TEST_F(DynamicModulesLoadBalancerTest, MetricsCounterDefineAndIncrement) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto* typed_config =
      dynamic_cast<const TypedDynamicModuleLbConfig*>(lb_config_or_error.value().get());
  ASSERT_NE(typed_config, nullptr);
  auto lb_config = typed_config->config();
  auto* config_ptr = static_cast<void*>(lb_config.get());

  // Define a counter (no labels).
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_counter", .length = 12};
  size_t counter_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_define_counter(config_ptr, name, nullptr, 0,
                                                                   &counter_id));
  EXPECT_EQ(1, counter_id);

  // Increment the counter via config pointer.
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_increment_counter(config_ptr, counter_id,
                                                                      nullptr, 0, 5));

  // Verify the counter value via stats store.
  auto counter =
      TestUtility::findCounter(factory_context_.store_, "dynamicmodulescustom.test_counter");
  ASSERT_NE(nullptr, counter);
  EXPECT_EQ(5, counter->value());

  // Increment again.
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_increment_counter(config_ptr, counter_id,
                                                                      nullptr, 0, 3));
  EXPECT_EQ(8, counter->value());
}

TEST_F(DynamicModulesLoadBalancerTest, MetricsGaugeDefineAndManipulate) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto* typed_config =
      dynamic_cast<const TypedDynamicModuleLbConfig*>(lb_config_or_error.value().get());
  auto lb_config = typed_config->config();
  auto* config_ptr = static_cast<void*>(lb_config.get());

  // Define a gauge (no labels).
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_gauge", .length = 10};
  size_t gauge_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_define_gauge(config_ptr, name, nullptr, 0,
                                                                 &gauge_id));
  EXPECT_EQ(1, gauge_id);

  // Set gauge.
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_Success,
      envoy_dynamic_module_callback_lb_config_set_gauge(config_ptr, gauge_id, nullptr, 0, 100));

  // Increment gauge.
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_increment_gauge(config_ptr, gauge_id, nullptr,
                                                                    0, 10));

  // Decrement gauge.
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_Success,
      envoy_dynamic_module_callback_lb_config_decrement_gauge(config_ptr, gauge_id, nullptr, 0, 5));

  // Verify: 100 + 10 - 5 = 105.
  auto gauge = TestUtility::findGauge(factory_context_.store_, "dynamicmodulescustom.test_gauge");
  ASSERT_NE(nullptr, gauge);
  EXPECT_EQ(105, gauge->value());
}

TEST_F(DynamicModulesLoadBalancerTest, MetricsHistogramDefineAndRecord) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto* typed_config =
      dynamic_cast<const TypedDynamicModuleLbConfig*>(lb_config_or_error.value().get());
  auto lb_config = typed_config->config();
  auto* config_ptr = static_cast<void*>(lb_config.get());

  // Define a histogram (no labels).
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_histogram", .length = 14};
  size_t histogram_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_define_histogram(config_ptr, name, nullptr, 0,
                                                                     &histogram_id));
  EXPECT_EQ(1, histogram_id);

  // Record a histogram value.
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_record_histogram_value(config_ptr, histogram_id,
                                                                           nullptr, 0, 42));
}

TEST_F(DynamicModulesLoadBalancerTest, MetricsInvalidId) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto* typed_config =
      dynamic_cast<const TypedDynamicModuleLbConfig*>(lb_config_or_error.value().get());
  auto lb_config = typed_config->config();
  auto* config_ptr = static_cast<void*>(lb_config.get());

  // Using invalid IDs should return MetricNotFound (no labels).
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_MetricNotFound,
      envoy_dynamic_module_callback_lb_config_increment_counter(config_ptr, 999, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_lb_config_set_gauge(config_ptr, 999, nullptr, 0, 1));
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_MetricNotFound,
      envoy_dynamic_module_callback_lb_config_increment_gauge(config_ptr, 999, nullptr, 0, 1));
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_MetricNotFound,
      envoy_dynamic_module_callback_lb_config_decrement_gauge(config_ptr, 999, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_lb_config_record_histogram_value(config_ptr, 999, nullptr,
                                                                           0, 1));

  // ID 0 should also return MetricNotFound (1-based IDs).
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_MetricNotFound,
      envoy_dynamic_module_callback_lb_config_increment_counter(config_ptr, 0, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_lb_config_set_gauge(config_ptr, 0, nullptr, 0, 1));
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_MetricNotFound,
      envoy_dynamic_module_callback_lb_config_record_histogram_value(config_ptr, 0, nullptr, 0, 1));

  // Using invalid IDs with labels should also return MetricNotFound.
  envoy_dynamic_module_type_module_buffer label_val = {.ptr = "val", .length = 3};
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_MetricNotFound,
      envoy_dynamic_module_callback_lb_config_increment_counter(config_ptr, 999, &label_val, 1, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_lb_config_set_gauge(config_ptr, 999, &label_val, 1, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_lb_config_record_histogram_value(config_ptr, 999,
                                                                           &label_val, 1, 1));
}

TEST_F(DynamicModulesLoadBalancerTest, MetricsMultipleCounters) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto* typed_config =
      dynamic_cast<const TypedDynamicModuleLbConfig*>(lb_config_or_error.value().get());
  auto lb_config = typed_config->config();
  auto* config_ptr = static_cast<void*>(lb_config.get());

  // Define two counters.
  envoy_dynamic_module_type_module_buffer name1 = {.ptr = "counter_a", .length = 9};
  envoy_dynamic_module_type_module_buffer name2 = {.ptr = "counter_b", .length = 9};
  size_t id1 = 0, id2 = 0;
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_Success,
      envoy_dynamic_module_callback_lb_config_define_counter(config_ptr, name1, nullptr, 0, &id1));
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_Success,
      envoy_dynamic_module_callback_lb_config_define_counter(config_ptr, name2, nullptr, 0, &id2));
  EXPECT_EQ(1, id1);
  EXPECT_EQ(2, id2);

  // Increment each counter independently.
  envoy_dynamic_module_callback_lb_config_increment_counter(config_ptr, id1, nullptr, 0, 10);
  envoy_dynamic_module_callback_lb_config_increment_counter(config_ptr, id2, nullptr, 0, 20);

  auto counter_a =
      TestUtility::findCounter(factory_context_.store_, "dynamicmodulescustom.counter_a");
  auto counter_b =
      TestUtility::findCounter(factory_context_.store_, "dynamicmodulescustom.counter_b");
  ASSERT_NE(nullptr, counter_a);
  ASSERT_NE(nullptr, counter_b);
  EXPECT_EQ(10, counter_a->value());
  EXPECT_EQ(20, counter_b->value());
}

TEST_F(DynamicModulesLoadBalancerTest, MetricsCounterVecWithLabels) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto* typed_config =
      dynamic_cast<const TypedDynamicModuleLbConfig*>(lb_config_or_error.value().get());
  auto lb_config = typed_config->config();
  auto* config_ptr = static_cast<void*>(lb_config.get());

  // Define a counter vec with two labels.
  envoy_dynamic_module_type_module_buffer name = {.ptr = "req_total", .length = 9};
  envoy_dynamic_module_type_module_buffer label_names[2] = {{.ptr = "method", .length = 6},
                                                            {.ptr = "status", .length = 6}};
  size_t counter_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_define_counter(config_ptr, name, label_names, 2,
                                                                   &counter_id));
  EXPECT_EQ(1, counter_id);

  // Increment with matching label values.
  envoy_dynamic_module_type_module_buffer label_values[2] = {{.ptr = "GET", .length = 3},
                                                             {.ptr = "200", .length = 3}};
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_increment_counter(config_ptr, counter_id,
                                                                      label_values, 2, 1));

  // Increment with different label values.
  envoy_dynamic_module_type_module_buffer label_values2[2] = {{.ptr = "POST", .length = 4},
                                                              {.ptr = "500", .length = 3}};
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_increment_counter(config_ptr, counter_id,
                                                                      label_values2, 2, 3));

  // Wrong number of label values should return InvalidLabels.
  envoy_dynamic_module_type_module_buffer single_val = {.ptr = "GET", .length = 3};
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_lb_config_increment_counter(config_ptr, counter_id,
                                                                      &single_val, 1, 1));
}

TEST_F(DynamicModulesLoadBalancerTest, MetricsGaugeVecWithLabels) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto* typed_config =
      dynamic_cast<const TypedDynamicModuleLbConfig*>(lb_config_or_error.value().get());
  auto lb_config = typed_config->config();
  auto* config_ptr = static_cast<void*>(lb_config.get());

  // Define a gauge vec with one label.
  envoy_dynamic_module_type_module_buffer name = {.ptr = "active_conns", .length = 12};
  envoy_dynamic_module_type_module_buffer label_name = {.ptr = "backend", .length = 7};
  size_t gauge_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_define_gauge(config_ptr, name, &label_name, 1,
                                                                 &gauge_id));
  EXPECT_EQ(1, gauge_id);

  // Set, increment, decrement with labels.
  envoy_dynamic_module_type_module_buffer label_value = {.ptr = "host1", .length = 5};
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_Success,
      envoy_dynamic_module_callback_lb_config_set_gauge(config_ptr, gauge_id, &label_value, 1, 50));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_increment_gauge(config_ptr, gauge_id,
                                                                    &label_value, 1, 10));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_decrement_gauge(config_ptr, gauge_id,
                                                                    &label_value, 1, 5));

  // Wrong label count should return InvalidLabels.
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_InvalidLabels,
      envoy_dynamic_module_callback_lb_config_set_gauge(config_ptr, gauge_id, nullptr, 0, 10));
}

TEST_F(DynamicModulesLoadBalancerTest, MetricsHistogramVecWithLabels) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto* typed_config =
      dynamic_cast<const TypedDynamicModuleLbConfig*>(lb_config_or_error.value().get());
  auto lb_config = typed_config->config();
  auto* config_ptr = static_cast<void*>(lb_config.get());

  // Define a histogram vec with one label.
  envoy_dynamic_module_type_module_buffer name = {.ptr = "latency", .length = 7};
  envoy_dynamic_module_type_module_buffer label_name = {.ptr = "endpoint", .length = 8};
  size_t histogram_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_define_histogram(config_ptr, name, &label_name,
                                                                     1, &histogram_id));
  EXPECT_EQ(1, histogram_id);

  // Record histogram values with labels.
  envoy_dynamic_module_type_module_buffer label_value = {.ptr = "10.0.0.1", .length = 8};
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_record_histogram_value(config_ptr, histogram_id,
                                                                           &label_value, 1, 42));

  // Wrong label count should return InvalidLabels.
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_lb_config_record_histogram_value(config_ptr, histogram_id,
                                                                           nullptr, 0, 10));

  // Wrong label count (too many) should return InvalidLabels.
  envoy_dynamic_module_type_module_buffer extra_labels[2] = {{.ptr = "a", .length = 1},
                                                             {.ptr = "b", .length = 1}};
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_lb_config_record_histogram_value(config_ptr, histogram_id,
                                                                           extra_labels, 2, 10));
}

TEST_F(DynamicModulesLoadBalancerTest, MetricsVecScalarIdConflictErrors) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto* typed_config =
      dynamic_cast<const TypedDynamicModuleLbConfig*>(lb_config_or_error.value().get());
  auto lb_config = typed_config->config();
  auto* config_ptr = static_cast<void*>(lb_config.get());

  // Define a counter vec (ID 1 in vec space).
  envoy_dynamic_module_type_module_buffer counter_name = {.ptr = "cv", .length = 2};
  envoy_dynamic_module_type_module_buffer label_name = {.ptr = "lbl", .length = 3};
  size_t counter_vec_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_define_counter(
                config_ptr, counter_name, &label_name, 1, &counter_vec_id));

  // Calling increment_counter with 0 labels on a vec ID returns InvalidLabels.
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_lb_config_increment_counter(config_ptr, counter_vec_id,
                                                                      nullptr, 0, 1));

  // Define a gauge vec (ID 1 in vec space).
  envoy_dynamic_module_type_module_buffer gauge_name = {.ptr = "gv", .length = 2};
  size_t gauge_vec_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_define_gauge(config_ptr, gauge_name,
                                                                 &label_name, 1, &gauge_vec_id));

  // Calling set_gauge, increment_gauge, decrement_gauge with 0 labels on a vec ID returns
  // InvalidLabels.
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_InvalidLabels,
      envoy_dynamic_module_callback_lb_config_set_gauge(config_ptr, gauge_vec_id, nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_lb_config_increment_gauge(config_ptr, gauge_vec_id,
                                                                    nullptr, 0, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_lb_config_decrement_gauge(config_ptr, gauge_vec_id,
                                                                    nullptr, 0, 1));

  // Define a histogram vec (ID 1 in vec space).
  envoy_dynamic_module_type_module_buffer hist_name = {.ptr = "hv", .length = 2};
  size_t hist_vec_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_define_histogram(config_ptr, hist_name,
                                                                     &label_name, 1, &hist_vec_id));

  // Calling record_histogram_value with 0 labels on a vec ID returns InvalidLabels.
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_lb_config_record_histogram_value(config_ptr, hist_vec_id,
                                                                           nullptr, 0, 1));
}

TEST_F(DynamicModulesLoadBalancerTest, MetricsVecWrongLabelCount) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto* typed_config =
      dynamic_cast<const TypedDynamicModuleLbConfig*>(lb_config_or_error.value().get());
  auto lb_config = typed_config->config();
  auto* config_ptr = static_cast<void*>(lb_config.get());

  // Define a gauge vec with one label.
  envoy_dynamic_module_type_module_buffer gauge_name = {.ptr = "gwl", .length = 3};
  envoy_dynamic_module_type_module_buffer label_name = {.ptr = "lbl", .length = 3};
  size_t gauge_vec_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_lb_config_define_gauge(config_ptr, gauge_name,
                                                                 &label_name, 1, &gauge_vec_id));

  // Providing wrong number of label values (2 instead of 1).
  envoy_dynamic_module_type_module_buffer extra_vals[2] = {{.ptr = "a", .length = 1},
                                                           {.ptr = "b", .length = 1}};
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_lb_config_set_gauge(config_ptr, gauge_vec_id, extra_vals,
                                                              2, 50));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_lb_config_increment_gauge(config_ptr, gauge_vec_id,
                                                                    extra_vals, 2, 10));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_InvalidLabels,
            envoy_dynamic_module_callback_lb_config_decrement_gauge(config_ptr, gauge_vec_id,
                                                                    extra_vals, 2, 5));
}

TEST_F(DynamicModulesLoadBalancerTest, MetricsVecNotFoundWithLabels) {
  envoy::extensions::load_balancing_policies::dynamic_modules::v3::DynamicModulesLoadBalancerConfig
      config;
  config.mutable_dynamic_module_config()->set_name("lb_round_robin");
  config.set_lb_policy_name("test_lb");

  Factory factory;
  auto lb_config_or_error = factory.loadConfig(factory_context_, config);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto* typed_config =
      dynamic_cast<const TypedDynamicModuleLbConfig*>(lb_config_or_error.value().get());
  auto lb_config = typed_config->config();
  auto* config_ptr = static_cast<void*>(lb_config.get());

  // Using non-existent vec IDs with labels should return MetricNotFound.
  envoy_dynamic_module_type_module_buffer label_val = {.ptr = "val", .length = 3};
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_MetricNotFound,
      envoy_dynamic_module_callback_lb_config_increment_gauge(config_ptr, 999, &label_val, 1, 10));
  EXPECT_EQ(
      envoy_dynamic_module_type_metrics_result_MetricNotFound,
      envoy_dynamic_module_callback_lb_config_decrement_gauge(config_ptr, 999, &label_val, 1, 5));
}

} // namespace
} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
