#include "contrib/per_worker_subset/load_balancing_policies/source/config.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PerWorkerSubset {

namespace {
PartitioningStrategy toStrategy(PerWorkerSubsetLbProto::PartitioningStrategy proto_strategy) {
  switch (proto_strategy) {
  case PerWorkerSubsetLbProto::RANDOM_PARTITIONS:
    return PartitioningStrategy::RandomPartitions;
  case PerWorkerSubsetLbProto::EQUAL_PARTITIONS:
  default:
    return PartitioningStrategy::EqualPartitions;
  }
}

HostSelectionStrategy
toHostSelectionStrategy(PerWorkerSubsetLbProto::HostSelectionStrategy proto_strategy) {
  switch (proto_strategy) {
  case PerWorkerSubsetLbProto::SIMPLE_ROUND_ROBIN:
    return HostSelectionStrategy::SimpleRoundRobin;
  case PerWorkerSubsetLbProto::ENVOY_ROUND_ROBIN:
    return HostSelectionStrategy::EnvoyRoundRobin;
  case PerWorkerSubsetLbProto::ENVOY_P2C:
    return HostSelectionStrategy::EnvoyP2C;
  case PerWorkerSubsetLbProto::UNSPECIFIED:
  default:
    return HostSelectionStrategy::Unspecified;
  }
}
} // namespace

Upstream::LoadBalancerPtr PerWorkerSubsetCreator::operator()(
    Upstream::LoadBalancerParams params, OptRef<const Upstream::LoadBalancerConfig> lb_config,
    const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet& /*priority_set*/,
    Runtime::Loader& runtime, ::Envoy::Random::RandomGenerator& random, TimeSource& time_source) {
  const auto* typed = dynamic_cast<const TypedPerWorkerSubsetLbConfig*>(lb_config.ptr());
  ASSERT(typed != nullptr, "per_worker_subset: invalid load balancer config");

  // Sequential per-cluster worker ID. ``fetch_add`` returns the
  // pre-increment value, which serves as this worker's unique ID.
  // ``worker_id`` is independent of ``total_workers_``, which was resolved
  // up-front at config-load time from ``bootstrap.concurrency``.
  const uint32_t worker_id = typed->next_worker_id_.fetch_add(1, std::memory_order_relaxed);

  // Per-worker fallback threshold from the extension proto. Unset -> 50
  // (matches the historical default). Range validated ``[0, 100]`` in
  // ``Factory::loadConfig``.
  const uint32_t fallback_threshold =
      typed->proto_.has_fallback_threshold() ? typed->proto_.fallback_threshold().value() : 50;

  return std::make_unique<PerWorkerSubsetLoadBalancer>(
      params.priority_set, cluster_info.lbStats(), cluster_info.statsScope(), runtime, random,
      time_source, typed->proto_.subset_size(), toStrategy(typed->proto_.partitioning_strategy()),
      toHostSelectionStrategy(typed->proto_.host_selection_strategy()), worker_id,
      typed->total_workers_, fallback_threshold, typed->envoy_seed_,
      typed->proto_.slow_start_config());
}

REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace PerWorkerSubset
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
