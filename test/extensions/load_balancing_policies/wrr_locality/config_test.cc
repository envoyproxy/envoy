#include "envoy/config/core/v3/extension.pb.h"

#include "source/extensions/load_balancing_policies/wrr_locality/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace WrrLocality {
namespace {

TEST(WrrLocalityConfigTest, ValidateSuccess) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Upstream::MockClusterInfo> cluster_info;
  NiceMock<Upstream::MockPrioritySet> main_thread_priority_set;
  NiceMock<Upstream::MockPrioritySet> thread_local_priority_set;
  NiceMock<Event::MockDispatcher> mock_thread_dispatcher;
  ON_CALL(context, mainThreadDispatcher()).WillByDefault(ReturnRef(mock_thread_dispatcher));

  // Client-side weighted round robin policy for endpoint picking.
  envoy::extensions::load_balancing_policies::client_side_weighted_round_robin::v3::
      ClientSideWeightedRoundRobin cswrr_config_msg;
  envoy::config::core::v3::TypedExtensionConfig cswrr_config;
  cswrr_config.set_name("envoy.load_balancing_policies.client_side_weighted_round_robin");
  cswrr_config.mutable_typed_config()->PackFrom(cswrr_config_msg);

  // WrrLocality policy with ClientSideWeightedRoundRobin policy for endpoint
  // picking.
  envoy::extensions::load_balancing_policies::wrr_locality::v3::WrrLocality wrr_locality_config_msg;
  *(wrr_locality_config_msg.mutable_endpoint_picking_policy()
        ->add_policies()
        ->mutable_typed_extension_config()) = cswrr_config;

  envoy::config::core::v3::TypedExtensionConfig wrr_locality_config;
  wrr_locality_config.set_name("envoy.load_balancing_policies.wrr_locality");
  wrr_locality_config.mutable_typed_config()->PackFrom(wrr_locality_config_msg);

  auto& factory =
      Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(wrr_locality_config);
  EXPECT_EQ("envoy.load_balancing_policies.wrr_locality", factory.name());

  auto lb_config = factory.loadConfig(context, wrr_locality_config_msg).value();

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

TEST(WrrLocalityConfigTest, ValidateFailureWithoutEndpointPickingPolicy) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  // WrrLocality policy without endpoint picking policy.
  envoy::extensions::load_balancing_policies::wrr_locality::v3::WrrLocality wrr_locality_config_msg;
  envoy::config::core::v3::TypedExtensionConfig wrr_locality_config;
  wrr_locality_config.set_name("envoy.load_balancing_policies.wrr_locality");
  wrr_locality_config.mutable_typed_config()->PackFrom(wrr_locality_config_msg);

  auto& factory =
      Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(wrr_locality_config);
  EXPECT_EQ("envoy.load_balancing_policies.wrr_locality", factory.name());

  EXPECT_EQ(factory.loadConfig(context, wrr_locality_config_msg).status(),
            absl::InvalidArgumentError("No supported endpoint picking policy."));
}

TEST(WrrLocalityConfigTest, ValidateFailureUnsupportedEndpointPickingPolicy) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  // WrrLocality policy WITHOUT ClientSideWeightedRoundRobin policy for endpoint
  // picking is currently not supported.
  // Random lb policy for endpoint picking.
  envoy::extensions::load_balancing_policies::random::v3::Random epp_config_msg;
  envoy::config::core::v3::TypedExtensionConfig epp_config;

  epp_config.set_name("envoy.load_balancing_policies.random");
  epp_config.mutable_typed_config()->PackFrom(epp_config_msg);

  // WrrLocality policy with Random policy for endpoint picking.
  envoy::extensions::load_balancing_policies::wrr_locality::v3::WrrLocality wrr_locality_config_msg;
  *(wrr_locality_config_msg.mutable_endpoint_picking_policy()
        ->add_policies()
        ->mutable_typed_extension_config()) = epp_config;

  envoy::config::core::v3::TypedExtensionConfig wrr_locality_config;
  wrr_locality_config.set_name("envoy.load_balancing_policies.wrr_locality");
  wrr_locality_config.mutable_typed_config()->PackFrom(wrr_locality_config_msg);

  auto& factory =
      Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(wrr_locality_config);
  EXPECT_EQ("envoy.load_balancing_policies.wrr_locality", factory.name());

  EXPECT_EQ(factory.loadConfig(context, wrr_locality_config_msg).status(),
            absl::InvalidArgumentError("Currently WrrLocalityLoadBalancer only supports "
                                       "ClientSideWeightedRoundRobinLoadBalancer as its endpoint "
                                       "picking policy."));
}

} // namespace
} // namespace WrrLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
