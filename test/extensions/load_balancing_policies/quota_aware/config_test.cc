#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/load_balancing_policies/quota_aware/v3/quota_aware.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/config/utility.h"
#include "source/extensions/load_balancing_policies/quota_aware/config.h"
#include "source/extensions/load_balancing_policies/quota_aware/load_balancer.h"
#include "source/extensions/load_balancing_policies/round_robin/config.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace QuotaAware {
namespace {

using ::envoy::extensions::load_balancing_policies::quota_aware::v3::QuotaAware;
using ::envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin;
using ::testing::HasSubstr;

void addRoundRobinFallback(QuotaAware& config) {
  auto* typed_extension_config =
      config.mutable_fallback_policy()->add_policies()->mutable_typed_extension_config();
  typed_extension_config->set_name("envoy.load_balancing_policies.round_robin");
  RoundRobin rr;
  std::ignore = typed_extension_config->mutable_typed_config()->PackFrom(rr);
}

TEST(QuotaAwareLbConfigTest, NoFallbackLb) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancing_policies.quota_aware");
  QuotaAware config_msg;
  std::ignore = config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);
  EXPECT_EQ("envoy.load_balancing_policies.quota_aware", factory.name());
  EXPECT_THROW_WITH_REGEX(factory.loadConfig(context, config_msg).value(), EnvoyException,
                          "value is required");
}

TEST(QuotaAwareLbConfigTest, NoFallbackPolicies) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancing_policies.quota_aware");
  QuotaAware config_msg;
  config_msg.mutable_fallback_policy();
  std::ignore = config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);
  auto result = factory.loadConfig(context, config_msg);
  EXPECT_THAT(result, StatusHelpers::HasStatus(
                          absl::StatusCode::kInvalidArgument,
                          HasSubstr("didn't find a registered fallback load balancer factory")));
}

TEST(QuotaAwareLbConfigTest, FirstValidFallbackPolicyIsUsed) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancing_policies.quota_aware");
  QuotaAware config_msg;

  auto* unknown =
      config_msg.mutable_fallback_policy()->add_policies()->mutable_typed_extension_config();
  unknown->set_name("not.a.real.policy");
  Protobuf::Struct empty;
  std::ignore = unknown->mutable_typed_config()->PackFrom(empty);
  addRoundRobinFallback(config_msg);

  std::ignore = config.mutable_typed_config()->PackFrom(config_msg);
  auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);
  auto result = factory.loadConfig(context, config_msg);
  EXPECT_OK(result);

  NiceMock<Upstream::MockClusterInfo> cluster_info;
  NiceMock<Upstream::MockPrioritySet> main_thread_priority_set;
  auto thread_aware_lb =
      factory.create(*result.value(), cluster_info, main_thread_priority_set,
                     context.runtime_loader_, context.api_.random_, context.time_system_);
  EXPECT_NE(nullptr, thread_aware_lb);
  ASSERT_OK(thread_aware_lb->initialize());
  EXPECT_NE(nullptr, thread_aware_lb->factory());
}

TEST(QuotaAwareLbConfigTest, EmptyConfigProto) {
  QuotaAwareLoadBalancerFactory factory;
  EXPECT_NE(nullptr, factory.createEmptyConfigProto());
  EXPECT_EQ("envoy.load_balancing_policies.quota_aware", factory.name());
}

TEST(QuotaAwareLbConfigTest, DefaultsMatchLockedContract) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  QuotaAware config_msg;
  addRoundRobinFallback(config_msg);

  auto result = QuotaAwareLoadBalancerFactory().loadConfig(context, config_msg);
  EXPECT_OK(result);
  const auto* cfg = dynamic_cast<const QuotaAwareLbConfig*>(result.value().get());
  ASSERT_NE(nullptr, cfg);
  EXPECT_EQ(kDefaultMetadataNamespace, cfg->metadataNamespace());
  EXPECT_EQ(kDefaultCandidatesKey, cfg->candidatesKey());
  EXPECT_EQ(kDefaultHostMetadataNamespace, cfg->hostMetadataNamespace());
  EXPECT_EQ(kDefaultHostBackendIdKey, cfg->hostBackendIdKey());
  EXPECT_EQ(kDefaultHostModelIdKey, cfg->hostModelIdKey());
  EXPECT_FALSE(cfg->failClosedOnMissingMetadata());
}

} // namespace
} // namespace QuotaAware
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
