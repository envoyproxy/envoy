#include "source/extensions/load_balancing_policies/override_host/config.h"

#include <memory>
#include <utility>

#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "envoy/extensions/load_balancing_policies/override_host/v3/override_host.pb.h"
#include "envoy/extensions/load_balancing_policies/override_host/v3/override_host.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/load_balancing_policies/override_host/load_balancer.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace DynamicForwarding {

using ::envoy::extensions::load_balancing_policies::override_host::v3::DynamicForwarding;

absl::StatusOr<Upstream::LoadBalancerConfigPtr> DynamicForwardingLoadBalancerFactory::loadConfig(
    Server::Configuration::ServerFactoryContext& context, const Protobuf::Message& config) {
  const DynamicForwarding& override_host_config =
      MessageUtil::downcastAndValidate<const DynamicForwarding&>(
          config, context.messageValidationVisitor());
  if (!override_host_config.has_fallback_picking_policy()) {
    return absl::InvalidArgumentError("The fallback picking policy must be set.");
  }
  return DynamicForwardingLbConfig::make(override_host_config, context);
}

Upstream::ThreadAwareLoadBalancerPtr
DynamicForwardingLoadBalancerFactory::create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                             const ClusterInfo& cluster_info,
                                             const PrioritySet& priority_set, Loader& runtime,
                                             RandomGenerator& random, TimeSource& time_source) {
  ASSERT(lb_config.has_value()); // Factory can not work without config.
  const auto& override_host_lb_config =
      dynamic_cast<const DynamicForwardingLbConfig&>(lb_config.ref());
  Upstream::ThreadAwareLoadBalancerPtr locality_picker_lb =
      override_host_lb_config.create(cluster_info, priority_set, runtime, random, time_source);
  ASSERT(locality_picker_lb != nullptr); // Factory can not create null LB.
  return std::make_unique<DynamicForwardingLoadBalancer>(override_host_lb_config,
                                                         std::move(locality_picker_lb));
}

REGISTER_FACTORY(DynamicForwardingLoadBalancerFactory, Upstream::TypedLoadBalancerFactory);

} // namespace DynamicForwarding
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
