#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/load_balancing_policies/host_allowlist/v3/host_allowlist.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/config/utility.h"
#include "source/extensions/load_balancing_policies/host_allowlist/config.h"
#include "source/extensions/load_balancing_policies/host_allowlist/load_balancer.h"
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
namespace HostAllowlist {
namespace {

using ::envoy::extensions::load_balancing_policies::host_allowlist::v3::HostAllowlist;
using ::envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin;
using ::testing::ElementsAre;
using ::testing::HasSubstr;

void fillRequiredFields(HostAllowlist& config) {
  config.set_metadata_namespace("envoy.filters.http.ratelimit");
  config.set_metadata_key("candidates");
  if (config.host_metadata_keys().empty()) {
    config.add_host_metadata_keys("id");
  }
}

void addRoundRobinChild(HostAllowlist& config) {
  fillRequiredFields(config);
  auto* typed_extension_config =
      config.mutable_child_policy()->add_policies()->mutable_typed_extension_config();
  typed_extension_config->set_name("envoy.load_balancing_policies.round_robin");
  RoundRobin rr;
  std::ignore = typed_extension_config->mutable_typed_config()->PackFrom(rr);
}

TEST(HostAllowlistLbConfigTest, NoFallbackLb) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancing_policies.host_allowlist");
  HostAllowlist config_msg;
  std::ignore = config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);
  EXPECT_EQ("envoy.load_balancing_policies.host_allowlist", factory.name());
  EXPECT_THROW_WITH_REGEX(factory.loadConfig(context, config_msg).value(), EnvoyException,
                          "value length must be at least 1 characters");
}

TEST(HostAllowlistLbConfigTest, NoFallbackPolicies) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancing_policies.host_allowlist");
  HostAllowlist config_msg;
  fillRequiredFields(config_msg);
  config_msg.mutable_child_policy();
  std::ignore = config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);
  auto result = factory.loadConfig(context, config_msg);
  EXPECT_THAT(result, StatusHelpers::HasStatus(
                          absl::StatusCode::kInvalidArgument,
                          HasSubstr("didn't find a registered child load balancer factory")));
}

TEST(HostAllowlistLbConfigTest, FirstValidFallbackPolicyIsUsed) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancing_policies.host_allowlist");
  HostAllowlist config_msg;

  auto* unknown =
      config_msg.mutable_child_policy()->add_policies()->mutable_typed_extension_config();
  unknown->set_name("not.a.real.policy");
  Protobuf::Struct empty;
  std::ignore = unknown->mutable_typed_config()->PackFrom(empty);
  addRoundRobinChild(config_msg);

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

TEST(HostAllowlistLbConfigTest, EmptyConfigProto) {
  HostAllowlistLoadBalancerFactory factory;
  EXPECT_NE(nullptr, factory.createEmptyConfigProto());
  EXPECT_EQ("envoy.load_balancing_policies.host_allowlist", factory.name());
}

TEST(HostAllowlistLbConfigTest, DefaultsAreGeneric) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  HostAllowlist config_msg;
  addRoundRobinChild(config_msg);

  auto result = HostAllowlistLoadBalancerFactory().loadConfig(context, config_msg);
  EXPECT_OK(result);
  const auto* cfg = dynamic_cast<const HostAllowlistLbConfig*>(result.value().get());
  ASSERT_NE(nullptr, cfg);
  EXPECT_EQ("envoy.filters.http.ratelimit", cfg->metadataNamespace());
  EXPECT_EQ("candidates", cfg->metadataKey());
  EXPECT_EQ(kDefaultHostMetadataNamespace, cfg->hostMetadataNamespace());
  EXPECT_THAT(cfg->hostMetadataKeys(), testing::ElementsAre("id"));
  EXPECT_FALSE(cfg->failClosedOnMissingMetadata());
}

} // namespace
} // namespace HostAllowlist
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
