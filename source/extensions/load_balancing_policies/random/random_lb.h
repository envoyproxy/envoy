#pragma once

#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Random load balancer that picks a random host out of all hosts.
 */
class RandomLoadBalancer : public ZoneAwareLoadBalancerBase {
public:
  RandomLoadBalancer(const PrioritySet& priority_set, const PrioritySet* local_priority_set,
                     ClusterLbStats& stats, Runtime::Loader& runtime,
                     Random::RandomGenerator& random,
                     const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
      : ZoneAwareLoadBalancerBase(
            priority_set, local_priority_set, stats, runtime, random,
            PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(common_config, healthy_panic_threshold,
                                                           100, 50),
            LoadBalancerConfigHelper::localityLbConfigFromCommonLbConfig(common_config)) {}

  RandomLoadBalancer(
      const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterLbStats& stats,
      Runtime::Loader& runtime, Random::RandomGenerator& random, uint32_t healthy_panic_threshold,
      const envoy::extensions::load_balancing_policies::random::v3::Random& random_config)
      : ZoneAwareLoadBalancerBase(
            priority_set, local_priority_set, stats, runtime, random, healthy_panic_threshold,
            LoadBalancerConfigHelper::localityLbConfigFromProto(random_config)) {}

  // Upstream::ZoneAwareLoadBalancerBase
  HostConstSharedPtr chooseHostOnce(LoadBalancerContext* context) override;
  HostConstSharedPtr peekAnotherHost(LoadBalancerContext* context) override;

protected:
  HostConstSharedPtr peekOrChoose(LoadBalancerContext* context, bool peek);
};

} // namespace Upstream
} // namespace Envoy
