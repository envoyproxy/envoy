#include "source/extensions/load_balancing_policies/maglev/config.h"

#include "envoy/extensions/load_balancing_policies/maglev/v3/maglev.pb.h"

#include "source/extensions/load_balancing_policies/maglev/maglev_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Maglev {

Upstream::ThreadAwareLoadBalancerPtr
Factory::create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                const Upstream::ClusterInfo& cluster_info,
                const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
                Random::RandomGenerator& random, TimeSource&) {

  const auto typed_lb_config = dynamic_cast<const Upstream::TypedMaglevLbConfig*>(lb_config.ptr());
  ASSERT(typed_lb_config != nullptr, "Invalid maglev load balancer config");

  return std::make_unique<Upstream::MaglevLoadBalancer>(
      priority_set, cluster_info.lbStats(), cluster_info.statsScope(), runtime, random,
      static_cast<uint32_t>(PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
          cluster_info.lbConfig(), healthy_panic_threshold, 100, 50)),
      typed_lb_config->lb_config_);
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace Maglev
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
