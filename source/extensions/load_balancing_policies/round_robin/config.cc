#include "source/extensions/load_balancing_policies/round_robin/config.h"

#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"

#include "source/common/upstream/load_balancer_impl.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace RoundRobin {

LegacyRoundRobinLbConfig::LegacyRoundRobinLbConfig(const ClusterProto& cluster) {
  if (cluster.has_round_robin_lb_config()) {
    lb_config_ = cluster.round_robin_lb_config();
  }
}

TypedRoundRobinLbConfig::TypedRoundRobinLbConfig(const RoundRobinLbProto& lb_config)
    : lb_config_(lb_config) {}

Upstream::LoadBalancerPtr RoundRobinCreator::operator()(
    Upstream::LoadBalancerParams params, OptRef<const Upstream::LoadBalancerConfig> lb_config,
    const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet&,
    Runtime::Loader& runtime, Random::RandomGenerator& random, TimeSource& time_source) {

  auto active_or_legacy =
      Common::ActiveOrLegacy<TypedRoundRobinLbConfig, LegacyRoundRobinLbConfig>::get(
          lb_config.ptr());

  // The load balancing policy configuration will be loaded and validated in the main thread when we
  // load the cluster configuration. So we can assume the configuration is valid here.
  ASSERT(active_or_legacy.hasLegacy() || active_or_legacy.hasActive(),
         "Invalid load balancing policy configuration for least request load balancer");

  if (active_or_legacy.hasActive()) {
    return std::make_unique<Upstream::RoundRobinLoadBalancer>(
        params.priority_set, params.local_priority_set, cluster_info.lbStats(), runtime, random,
        PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(cluster_info.lbConfig(),
                                                       healthy_panic_threshold, 100, 50),
        active_or_legacy.active()->lb_config_, time_source);
  } else {
    return std::make_unique<Upstream::RoundRobinLoadBalancer>(
        params.priority_set, params.local_priority_set, cluster_info.lbStats(), runtime, random,
        cluster_info.lbConfig(), active_or_legacy.legacy()->lbConfig(), time_source);
  }
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace RoundRobin
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
