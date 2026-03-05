#include "envoy/config/core/v3/extension.pb.h"

#include "source/extensions/load_balancing_policies/load_aware_locality/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {
namespace {

TEST(LoadAwareLocalityConfigTest, ValidateSuccessWithRoundRobin) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Upstream::MockClusterInfo> cluster_info;
  NiceMock<Upstream::MockPrioritySet> main_thread_priority_set;
  NiceMock<Upstream::MockPrioritySet> thread_local_priority_set;
  NiceMock<Event::MockDispatcher> mock_thread_dispatcher;
  ON_CALL(context, mainThreadDispatcher()).WillByDefault(ReturnRef(mock_thread_dispatcher));

  // Round robin policy for endpoint picking.
  envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin rr_config_msg;
  envoy::config::core::v3::TypedExtensionConfig rr_config;
  rr_config.set_name("envoy.load_balancing_policies.round_robin");
  rr_config.mutable_typed_config()->PackFrom(rr_config_msg);

  // LoadAwareLocality policy with RoundRobin for endpoint picking.
  LoadAwareLocalityLbProto load_aware_config_msg;
  *(load_aware_config_msg.mutable_endpoint_picking_policy()
        ->add_policies()
        ->mutable_typed_extension_config()) = rr_config;

  envoy::config::core::v3::TypedExtensionConfig load_aware_config;
  load_aware_config.set_name("envoy.load_balancing_policies.load_aware_locality");
  load_aware_config.mutable_typed_config()->PackFrom(load_aware_config_msg);

  auto& factory =
      Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(load_aware_config);
  EXPECT_EQ("envoy.load_balancing_policies.load_aware_locality", factory.name());

  auto lb_config = factory.loadConfig(context, load_aware_config_msg).value();

  auto thread_aware_lb =
      factory.create(*lb_config, cluster_info, main_thread_priority_set, context.runtime_loader_,
                     context.api_.random_, context.time_system_);
  EXPECT_NE(nullptr, thread_aware_lb);

  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  auto thread_local_lb_factory = thread_aware_lb->factory();
  EXPECT_NE(nullptr, thread_local_lb_factory);

  auto thread_local_lb = thread_local_lb_factory->create({thread_local_priority_set, nullptr});
  EXPECT_NE(nullptr, thread_local_lb);
}

TEST(LoadAwareLocalityConfigTest, ValidateFailureWithoutEndpointPickingPolicy) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  // LoadAwareLocality policy without endpoint picking policy.
  LoadAwareLocalityLbProto load_aware_config_msg;
  envoy::config::core::v3::TypedExtensionConfig load_aware_config;
  load_aware_config.set_name("envoy.load_balancing_policies.load_aware_locality");
  load_aware_config.mutable_typed_config()->PackFrom(load_aware_config_msg);

  auto& factory =
      Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(load_aware_config);
  EXPECT_EQ("envoy.load_balancing_policies.load_aware_locality", factory.name());

  EXPECT_EQ(factory.loadConfig(context, load_aware_config_msg).status(),
            absl::InvalidArgumentError("No supported endpoint picking policy."));
}

// Test: The four config fields that config.cc actually parses are accepted and used.
// Note: blackout_period, weight_expiration_period, error_utilization_penalty, and
// metric_names_for_computing_utilization are proto fields but are NOT currently parsed
// by config.cc — they exist for forward compatibility with future ORCA integration.
TEST(LoadAwareLocalityConfigTest, CustomParsedParams) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Upstream::MockClusterInfo> cluster_info;
  NiceMock<Upstream::MockPrioritySet> main_thread_priority_set;
  NiceMock<Event::MockDispatcher> mock_thread_dispatcher;
  ON_CALL(context, mainThreadDispatcher()).WillByDefault(ReturnRef(mock_thread_dispatcher));

  // Round robin policy for endpoint picking.
  envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin rr_config_msg;
  envoy::config::core::v3::TypedExtensionConfig rr_config;
  rr_config.set_name("envoy.load_balancing_policies.round_robin");
  rr_config.mutable_typed_config()->PackFrom(rr_config_msg);

  // Set only the fields that config.cc actually parses.
  LoadAwareLocalityLbProto load_aware_config_msg;
  *(load_aware_config_msg.mutable_endpoint_picking_policy()
        ->add_policies()
        ->mutable_typed_extension_config()) = rr_config;
  load_aware_config_msg.mutable_weight_update_period()->set_seconds(2);
  load_aware_config_msg.mutable_utilization_variance_threshold()->set_value(0.05);
  load_aware_config_msg.mutable_ewma_alpha()->set_value(0.5);
  load_aware_config_msg.mutable_probe_percentage()->set_value(0.05);

  envoy::config::core::v3::TypedExtensionConfig load_aware_config;
  load_aware_config.set_name("envoy.load_balancing_policies.load_aware_locality");
  load_aware_config.mutable_typed_config()->PackFrom(load_aware_config_msg);

  auto& factory =
      Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(load_aware_config);
  auto lb_config_or_error = factory.loadConfig(context, load_aware_config_msg);
  ASSERT_TRUE(lb_config_or_error.ok());

  auto thread_aware_lb =
      factory.create(*lb_config_or_error.value(), cluster_info, main_thread_priority_set,
                     context.runtime_loader_, context.api_.random_, context.time_system_);
  EXPECT_NE(nullptr, thread_aware_lb);
  ASSERT_TRUE(thread_aware_lb->initialize().ok());
}

} // namespace
} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
