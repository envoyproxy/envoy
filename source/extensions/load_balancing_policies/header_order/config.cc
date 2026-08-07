#include "source/extensions/load_balancing_policies/header_order/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/load_balancing_policies/header_order/load_balancer.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace HeaderOrder {

using ::envoy::extensions::load_balancing_policies::header_order::v3::HeaderOrder;

absl::StatusOr<Upstream::LoadBalancerConfigPtr>
HeaderOrderLoadBalancerFactory::loadConfig(Server::Configuration::ServerFactoryContext& context,
                                           const Protobuf::Message& config) {
  const HeaderOrder& header_order_config =
      MessageUtil::downcastAndValidate<const HeaderOrder&>(config, context.messageValidationVisitor());
  // Enforced in config validation.
  ASSERT(header_order_config.has_fallback_policy());
  return HeaderOrderLbConfig::make(header_order_config, context);
}

Upstream::ThreadAwareLoadBalancerPtr
HeaderOrderLoadBalancerFactory::create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                       const ClusterInfo& cluster_info,
                                       const PrioritySet& priority_set, Loader& runtime,
                                       RandomGenerator& random, TimeSource& time_source) {
  ASSERT(lb_config.has_value()); // Factory can not work without config.
  const auto& header_order_lb_config = dynamic_cast<const HeaderOrderLbConfig&>(lb_config.ref());
  Upstream::ThreadAwareLoadBalancerPtr fallback_lb =
      header_order_lb_config.create(cluster_info, priority_set, runtime, random, time_source);
  ASSERT(fallback_lb != nullptr); // Factory can not create null LB.
  return std::make_unique<HeaderOrderLoadBalancer>(header_order_lb_config, std::move(fallback_lb));
}

REGISTER_FACTORY(HeaderOrderLoadBalancerFactory, Upstream::TypedLoadBalancerFactory);

} // namespace HeaderOrder
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
