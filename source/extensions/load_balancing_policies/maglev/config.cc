#include "source/extensions/load_balancing_policies/maglev/config.h"

#include "envoy/extensions/load_balancing_policies/maglev/v3/maglev.pb.h"

#include "source/common/upstream/maglev_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Maglev {

Upstream::ThreadAwareLoadBalancerPtr Factory::create(const Upstream::ClusterInfo& cluster_info,
                                                     const Upstream::PrioritySet& priority_set,
                                                     Runtime::Loader& runtime,
                                                     Random::RandomGenerator& random, TimeSource&) {

  const auto* typed_config =
      dynamic_cast<const envoy::extensions::load_balancing_policies::maglev::v3::Maglev*>(
          cluster_info.loadBalancingPolicy().get());

  // The load balancing policy configuration will be loaded and validated in the main thread when we
  // load the cluster configuration. So we can assume the configuration is valid here.
  ASSERT(typed_config != nullptr,
         "Invalid load balancing policy configuration for maglev load balancer");

  return std::make_unique<Upstream::MaglevLoadBalancer>(
      priority_set, cluster_info.lbStats(), cluster_info.statsScope(), runtime, random,
      static_cast<uint32_t>(PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
          cluster_info.lbConfig(), healthy_panic_threshold, 100, 50)),
      *typed_config);
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace Maglev
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
