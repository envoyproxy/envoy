#include "source/extensions/load_balancing_policies/host_allowlist/config.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/load_balancing_policies/host_allowlist/v3/host_allowlist.pb.h"
#include "envoy/extensions/load_balancing_policies/host_allowlist/v3/host_allowlist.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/load_balancing_policies/host_allowlist/load_balancer.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace HostAllowlist {

using HostAllowlistProto =
    envoy::extensions::load_balancing_policies::host_allowlist::v3::HostAllowlist;

absl::StatusOr<Upstream::LoadBalancerConfigPtr>
HostAllowlistLoadBalancerFactory::loadConfig(Server::Configuration::ServerFactoryContext& context,
                                             const Protobuf::Message& config) {
  const HostAllowlistProto& host_allowlist_config =
      MessageUtil::downcastAndValidate<const HostAllowlistProto&>(
          config, context.messageValidationVisitor());
  ASSERT(host_allowlist_config.has_child_policy());
  return HostAllowlistLbConfig::make(host_allowlist_config, context);
}

Upstream::ThreadAwareLoadBalancerPtr
HostAllowlistLoadBalancerFactory::create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                         const ClusterInfo& cluster_info,
                                         const PrioritySet& priority_set, Loader& runtime,
                                         RandomGenerator& random, TimeSource& time_source) {
  ASSERT(lb_config.has_value());
  const auto& host_allowlist_lb_config =
      dynamic_cast<const HostAllowlistLbConfig&>(lb_config.ref());
  Upstream::ThreadAwareLoadBalancerPtr child_lb =
      host_allowlist_lb_config.create(cluster_info, priority_set, runtime, random, time_source);
  ASSERT(child_lb != nullptr);
  return std::make_unique<HostAllowlistLoadBalancer>(host_allowlist_lb_config, std::move(child_lb),
                                                     cluster_info.statsScope());
}

REGISTER_FACTORY(HostAllowlistLoadBalancerFactory, Upstream::TypedLoadBalancerFactory);

} // namespace HostAllowlist
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
