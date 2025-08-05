#include "source/extensions/load_balancing_policies/wrr_locality/wrr_locality_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace WrrLocality {

WrrLocalityLoadBalancer::WrrLocalityLoadBalancer(
    OptRef<const Upstream::LoadBalancerConfig> lb_config, const Upstream::ClusterInfo& cluster_info,
    const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
    Envoy::Random::RandomGenerator& random, TimeSource& time_source) {
  const auto* typed_lb_config = dynamic_cast<const WrrLocalityLbConfig*>(lb_config.ptr());
  ASSERT(typed_lb_config != nullptr);
  endpoint_picking_policy_ = typed_lb_config->endpoint_picking_policy_factory_.create(
      *typed_lb_config->endpoint_picking_policy_config_, cluster_info, priority_set, runtime,
      random, time_source);
  factory_ =
      std::make_shared<WorkerLocalLbFactory>(cluster_info, endpoint_picking_policy_->factory());
}

Upstream::LoadBalancerPtr
WrrLocalityLoadBalancer::WorkerLocalLbFactory::create(Upstream::LoadBalancerParams params) {
  // Ensure that the endpoint picking policy is a ClientSideWeightedRoundRobinLoadBalancer.
  auto* client_side_weighted_round_robin_factory = dynamic_cast<
      ::Envoy::Upstream::ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLbFactory*>(
      endpoint_picking_policy_factory_.get());
  if (client_side_weighted_round_robin_factory == nullptr) {
    return nullptr;
  }
  // Tell the worker local LB to use locality weights.
  auto lb_config = cluster_info_.lbConfig();
  lb_config.mutable_locality_weighted_lb_config();
  return client_side_weighted_round_robin_factory->createWithCommonLbConfig(lb_config, params);
}

} // namespace WrrLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
