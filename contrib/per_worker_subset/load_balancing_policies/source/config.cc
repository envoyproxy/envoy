#include "contrib/per_worker_subset/load_balancing_policies/source/config.h"

#include "source/common/common/assert.h"

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PerWorkerSubset {

namespace {
constexpr absl::string_view kWorkerNamePrefix = "worker_";

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

TypedPerWorkerSubsetLbConfig::TypedPerWorkerSubsetLbConfig(const PerWorkerSubsetLbProto& proto,
                                                           uint32_t total_workers,
                                                           uint64_t envoy_seed,
                                                           ThreadLocal::SlotAllocator& tls)
    : proto_(proto), total_workers_(total_workers), envoy_seed_(envoy_seed),
      worker_identity_(ThreadLocal::TypedSlot<WorkerIdentity>::makeUnique(tls)) {
  worker_identity_->set([total_workers](Event::Dispatcher& dispatcher) {
    absl::string_view name = dispatcher.name();
    uint32_t index = total_workers;
    if (absl::ConsumePrefix(&name, kWorkerNamePrefix) && absl::SimpleAtoi(name, &index)) {
      RELEASE_ASSERT(index < total_workers, "parsed worker index exceeds configured worker count");
    }
    // Non-worker threads can also receive TLS initialization callbacks. They
    // keep the sentinel value and never create a worker-local LB.
    return std::make_shared<WorkerIdentity>(index);
  });
}

uint32_t TypedPerWorkerSubsetLbConfig::workerId() const {
  const auto identity = worker_identity_->get();
  RELEASE_ASSERT(identity.has_value(), "per_worker_subset: worker identity TLS is unavailable");
  RELEASE_ASSERT(identity->index_ < total_workers_,
                 "per_worker_subset: LB creation attempted outside an Envoy worker thread");
  return identity->index_;
}

Upstream::LoadBalancerPtr PerWorkerSubsetCreator::operator()(
    Upstream::LoadBalancerParams params, OptRef<const Upstream::LoadBalancerConfig> lb_config,
    const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet& /*priority_set*/,
    Runtime::Loader& runtime, ::Envoy::Random::RandomGenerator& random, TimeSource& time_source) {
  const auto* typed = dynamic_cast<const TypedPerWorkerSubsetLbConfig*>(lb_config.ptr());
  ASSERT(typed != nullptr, "per_worker_subset: invalid load balancer config");

  // The TLS identity is stable for the worker's lifetime, so recreating this
  // LB cannot rotate the worker onto another partition.
  const uint32_t worker_id = typed->workerId();

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
