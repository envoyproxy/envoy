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

TEST(LoadAwareLocalityConfigTest, CreateFactory) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Upstream::MockClusterInfo> cluster_info;
  NiceMock<Upstream::MockPrioritySet> main_thread_priority_set;

  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancing_policies.load_aware_locality");
  LoadAwareLocalityProto config_msg;
  std::ignore = config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);
  EXPECT_EQ("envoy.load_balancing_policies.load_aware_locality", factory.name());

  // loadConfig returns UnimplementedError until the policy is functional.
  auto lb_config = factory.loadConfig(context, *factory.createEmptyConfigProto());
  EXPECT_FALSE(lb_config.ok());
  EXPECT_EQ(lb_config.status().code(), absl::StatusCode::kUnimplemented);

  // create is stubbed to return nullptr for now.
  auto thread_aware_lb =
      factory.create({}, cluster_info, main_thread_priority_set, context.runtime_loader_,
                     context.api_.random_, context.time_system_);
  EXPECT_EQ(nullptr, thread_aware_lb);
}

} // namespace
} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
