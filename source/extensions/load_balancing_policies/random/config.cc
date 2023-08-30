#include "source/extensions/load_balancing_policies/random/config.h"

#include "envoy/extensions/load_balancing_policies/random/v3/random.pb.h"

#include "source/common/upstream/load_balancer_impl.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Random {

Upstream::LoadBalancerPtr RandomCreator::operator()(
    Upstream::LoadBalancerParams params, OptRef<const Upstream::LoadBalancerConfig> lb_config,
    const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet&,
    Runtime::Loader& runtime, Envoy::Random::RandomGenerator& random, TimeSource&) {

  const auto typed_lb_config = dynamic_cast<const Upstream::TypedRandomLbConfig*>(lb_config.ptr());

  const auto legacy_lb_config =
      dynamic_cast<const Upstream::LegacyTypedRandomLbConfig*>(lb_config.ptr());

  // The load balancing policy configuration will be loaded and validated in the main thread
  // when we load the cluster configuration. So we can assume the configuration is valid here.
  ASSERT(typed_lb_config != nullptr || legacy_lb_config != nullptr,
         "Invalid load balancing policy configuration for random load balancer");

  if (typed_lb_config != nullptr) {
    return std::make_unique<Upstream::RandomLoadBalancer>(
        params.priority_set, params.local_priority_set, cluster_info.lbStats(), runtime, random,
        PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(cluster_info.lbConfig(),
                                                       healthy_panic_threshold, 100, 50),
        typed_lb_config->lb_config_);
  } else {
    return std::make_unique<Upstream::RandomLoadBalancer>(
        params.priority_set, params.local_priority_set, cluster_info.lbStats(), runtime, random,
        cluster_info.lbConfig());
  }
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace Random
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
