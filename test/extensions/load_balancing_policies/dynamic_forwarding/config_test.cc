#include <memory>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/load_balancing_policies/dynamic_forwarding/v3/dynamic_forwarding.pb.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/config/utility.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/load_balancing_policies/dynamic_forwarding/test_lb.pb.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace DynamicForwarding {
namespace {

using ::Envoy::EnvoyException;
using ::Envoy::Config::Utility;
using ::envoy::extensions::load_balancing_policies::dynamic_forwarding::v3::DynamicForwarding;
using ::Envoy::Upstream::MockHostSet;
using ::test::load_balancing_policies::dynamic_forwarding::Config;
using ::testing::HasSubstr;

TEST(DynamicForwardingLbonfigTest, NoFallbackLb) {
  NiceMock<Envoy::Server::Configuration::MockServerFactoryContext> context;

  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancers.dynamic_forwarding");
  DynamicForwarding config_msg;
  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Utility::getAndCheckFactory<::Envoy::Upstream::TypedLoadBalancerFactory>(config);
  EXPECT_EQ("envoy.load_balancing_policies.dynamic_forwarding", factory.name());

  EXPECT_THROW_WITH_REGEX(factory.loadConfig(context, config_msg).value(), EnvoyException,
                          "value is required");
}

TEST(DynamicForwardingLbonfigTest, NoFallbackPolicies) {
  NiceMock<Envoy::Server::Configuration::MockServerFactoryContext> context;

  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancers.dynamic_forwarding");
  DynamicForwarding config_msg;
  config_msg.mutable_fallback_picking_policy();
  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Utility::getAndCheckFactory<::Envoy::Upstream::TypedLoadBalancerFactory>(config);

  auto result = factory.loadConfig(context, config_msg);
  EXPECT_THAT(result, StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                               "dynamic forwarding LB: didn't find a registered "
                                               "fallback load balancer factory with names from "));
}

TEST(DynamicForwardingLbonfigTest, FirstValidFallbackPolicyIsUsed) {
  NiceMock<Envoy::Server::Configuration::MockServerFactoryContext> context;

  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancers.dynamic_forwarding");
  DynamicForwarding config_msg;

  ProtobufWkt::Struct invalid_policy;
  auto* typed_extension_config = config_msg.mutable_fallback_picking_policy()
                                     ->add_policies()
                                     ->mutable_typed_extension_config();
  typed_extension_config->mutable_typed_config()->PackFrom(invalid_policy);
  typed_extension_config->set_name("non_existent_policy");
  Config fallback_picker_config;
  typed_extension_config = config_msg.mutable_fallback_picking_policy()
                               ->add_policies()
                               ->mutable_typed_extension_config();
  typed_extension_config->mutable_typed_config()->PackFrom(fallback_picker_config);
  typed_extension_config->set_name("envoy.load_balancers.dynamic_forwarding.test");

  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Utility::getAndCheckFactory<::Envoy::Upstream::TypedLoadBalancerFactory>(config);

  auto result = factory.loadConfig(context, config_msg);
  EXPECT_TRUE(result.ok());
}

TEST(DynamicForwardingLbonfigTest, FallbackLbCalledToChooseHost) {
  NiceMock<Envoy::Server::Configuration::MockServerFactoryContext> context;
  auto cluster_info = std::make_shared<NiceMock<Envoy::Upstream::MockClusterInfo>>();
  NiceMock<Envoy::Upstream::MockPrioritySet> main_thread_priority_set;
  NiceMock<Envoy::Upstream::MockPrioritySet> thread_local_priority_set;
  NiceMock<Envoy::Upstream::MockLoadBalancerContext> load_balancer_context;

  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancers.dynamic_forwarding");
  DynamicForwarding config_msg;
  Config fallback_picker_config;
  auto* typed_extension_config = config_msg.mutable_fallback_picking_policy()
                                     ->add_policies()
                                     ->mutable_typed_extension_config();
  typed_extension_config->mutable_typed_config()->PackFrom(fallback_picker_config);
  typed_extension_config->set_name("envoy.load_balancers.dynamic_forwarding.test");
  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Utility::getAndCheckFactory<::Envoy::Upstream::TypedLoadBalancerFactory>(config);

  auto lb_config = factory.loadConfig(context, config_msg).value();

  auto thread_aware_lb =
      factory.create(*lb_config, *cluster_info, main_thread_priority_set, context.runtime_loader_,
                     context.api_.random_, context.time_system_);
  EXPECT_NE(thread_aware_lb, nullptr);

  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  auto thread_local_lb_factory = thread_aware_lb->factory();
  EXPECT_NE(thread_local_lb_factory, nullptr);

  MockHostSet* host_set = thread_local_priority_set.getMockHostSet(0);
  host_set->hosts_ = {
      Envoy::Upstream::makeTestHost(cluster_info, "tcp://127.0.0.1:80", context.time_system_)};
  host_set->runCallbacks(host_set->hosts_, {});
  auto thread_local_lb = thread_local_lb_factory->create({thread_local_priority_set, nullptr});
  EXPECT_NE(thread_local_lb, nullptr);

  auto host = thread_local_lb->chooseHost(&load_balancer_context).host;
  EXPECT_NE(host, nullptr);
  EXPECT_EQ(host->address()->asString(), "127.0.0.1:80");
}

} // namespace
} // namespace DynamicForwarding
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
