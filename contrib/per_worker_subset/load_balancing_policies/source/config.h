#pragma once

#include <thread>

#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"

#include "contrib/envoy/extensions/load_balancing_policies/per_worker_subset/v3alpha/per_worker_subset.pb.h"
#include "contrib/envoy/extensions/load_balancing_policies/per_worker_subset/v3alpha/per_worker_subset.pb.validate.h"
#include "contrib/per_worker_subset/load_balancing_policies/source/per_worker_subset_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PerWorkerSubset {

using ClusterProto = envoy::config::cluster::v3::Cluster;

struct PerWorkerSubsetCreator : public Logger::Loggable<Logger::Id::upstream> {
  Upstream::LoadBalancerPtr operator()(
      Upstream::LoadBalancerParams params, OptRef<const Upstream::LoadBalancerConfig> lb_config,
      const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet& priority_set,
      Runtime::Loader& runtime, ::Envoy::Random::RandomGenerator& random, TimeSource& time_source);
};

class Factory : public Common::FactoryBase<PerWorkerSubsetLbProto, PerWorkerSubsetCreator> {
public:
  Factory() : FactoryBase("envoy.load_balancing_policies.per_worker_subset") {}

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext& context,
             const Protobuf::Message& config) override {
    const auto* typed = dynamic_cast<const PerWorkerSubsetLbProto*>(&config);
    if (typed == nullptr) {
      return absl::InvalidArgumentError("per_worker_subset: unexpected config proto type");
    }
    // RANDOM_PARTITIONS requires an explicit positive ``subset_size`` --
    // there is no auto-K for it. EQUAL_PARTITIONS accepts 0 (= auto
    // K = ceil(N/W)) and any positive value (>= N disables subsetting).
    if (typed->partitioning_strategy() == PerWorkerSubsetLbProto::RANDOM_PARTITIONS &&
        typed->subset_size() == 0) {
      return absl::InvalidArgumentError("per_worker_subset: subset_size must be > 0 when "
                                        "partitioning_strategy=RANDOM_PARTITIONS");
    }
    // ``host_selection_strategy`` must be explicit. ``UNSPECIFIED`` is the
    // proto3-default sentinel; reject it with a clear message at config-load
    // instead of letting it default silently.
    if (typed->host_selection_strategy() == PerWorkerSubsetLbProto::UNSPECIFIED) {
      return absl::InvalidArgumentError(
          "per_worker_subset: host_selection_strategy must be set (UNSPECIFIED is a sentinel "
          "default; pick SIMPLE_ROUND_ROBIN, ENVOY_ROUND_ROBIN, or ENVOY_P2C)");
    }
    // ``fallback_threshold`` is in percent points, ``[0, 100]``. The proto
    // ``lte: 100`` validation catches this too; the explicit check here gives
    // a clearer error path for tooling that bypasses PGV.
    if (typed->has_fallback_threshold() && typed->fallback_threshold().value() > 100) {
      return absl::InvalidArgumentError(
          "per_worker_subset: fallback_threshold must be in [0, 100]");
    }
    // Process-local random seed for EQUAL_PARTITIONS' starting-offset
    // rotation. Bootstrap node IDs are optional and therefore cannot provide
    // a reliable per-Envoy identity. Stability across restarts is unnecessary:
    // the seed only decorrelates worker-to-host assignments across processes.
    const uint64_t envoy_seed = context.api().randomGenerator().random();
    return Upstream::LoadBalancerConfigPtr{
        new TypedPerWorkerSubsetLbConfig(*typed, resolveTotalWorkers(context), envoy_seed)};
  }

  // Legacy ``lb_policy`` enum path -- not supported for this extension.
  // Users must opt in via ``load_balancing_policy`` with the typed config.
  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadLegacy(Server::Configuration::ServerFactoryContext&, const ClusterProto&) override {
    return absl::InvalidArgumentError("per_worker_subset: legacy lb_policy config path is not "
                                      "supported; use load_balancing_policy");
  }

private:
  // Resolve total worker count W for EQUAL_PARTITIONS' ``K = ceil(N/W)``.
  // Priority order:
  //
  //   1. ``context.options().concurrency()`` -- Envoy's resolved worker
  //      count, populated from the ``--concurrency`` CLI flag if set,
  //      otherwise from Envoy's own CPU detection. Canonical source:
  //      whatever Envoy decided to use, this LB will agree with.
  //   2. ``std::thread::hardware_concurrency()`` -- defensive fallback for
  //      the unlikely case that ``options().concurrency()`` returns 0
  //      (should not happen in practice, but ``uint32`` zero is a valid
  //      return per the interface).
  //   3. Hard fallback 1 -- if even ``hardware_concurrency()`` returns 0
  //      (spec-allowed per ``[thread.thread.static]``), coerce to 1 so the
  //      K formula stays valid: ``K = ceil((N + 0) / 1) = N`` -- each
  //      worker holds the entire cluster, behaviorally equivalent to plain
  //      round-robin. No divide-by-zero, no crash; just loses the
  //      connection-count optimization in this degraded case.
  //
  // For cgroup-limited environments (Kubernetes, etc.), set
  // ``--concurrency`` on the envoy command line to match the cgroup CPU
  // limit. Without an explicit ``--concurrency``, Envoy defaults to host
  // CPU count, which does NOT account for cgroup limits and would cause
  // this LB to size K too small.
  static uint32_t resolveTotalWorkers(Server::Configuration::ServerFactoryContext& context) {
    uint32_t w = context.options().concurrency();
    if (w == 0) {
      w = std::thread::hardware_concurrency();
    }
    if (w == 0) {
      w = 1;
    }
    return w;
  }
};

DECLARE_FACTORY(Factory);

} // namespace PerWorkerSubset
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
