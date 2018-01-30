#pragma once

#include <cstdint>
#include <set>
#include <vector>

#include "envoy/api/v2/cds.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

/**
 * Base class for all LB implementations.
 */
class LoadBalancerBase {
public:
  // A utility function to chose a priority level based on a precomputed hash and
  // a priority vector in the style of per_priority_load_
  //
  // Returns the priority, a number between 0 and per_priority_load.size()-1
  static uint32_t choosePriority(uint64_t hash, const std::vector<uint32_t>& per_priority_load);

protected:
  /**
   * For the given host_set @return if we should be in a panic mode or not. For example, if the
   * majority of hosts are unhealthy we'll be likely in a panic mode. In this case we'll route
   * requests to hosts regardless of whether they are healthy or not.
   */
  static bool isGlobalPanic(const HostSet& host_set, Runtime::Loader& runtime);

  LoadBalancerBase(const PrioritySet& priority_set, ClusterStats& stats, Runtime::Loader& runtime,
                   Runtime::RandomGenerator& random);

  // Choose host set randomly, based on the per_priority_load_;
  const HostSet& chooseHostSet();

  uint32_t percentageLoad(uint32_t priority) const { return per_priority_load_[priority]; }

  ClusterStats& stats_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  // The priority-ordered set of hosts to use for load balancing.
  const PrioritySet& priority_set_;

  // Called when a host set at the given priority level is updated. This updates
  // per_priority_health_ for that priority level, and may update per_priority_load_ for all
  // priority levels.
  void recalculatePerPriorityState(uint32_t priority);

  // The percentage load (0-100) for each priority level
  std::vector<uint32_t> per_priority_load_;
  // The health (0-100) for each priority level.
  std::vector<uint32_t> per_priority_health_;
};

/**
 * Base class for zone aware load balancers
 */
class ZoneAwareLoadBalancerBase : public LoadBalancerBase {
protected:
  // Both priority_set and local_priority_set if non-null must have at least one host set.
  ZoneAwareLoadBalancerBase(const PrioritySet& priority_set, const PrioritySet* local_priority_set,
                            ClusterStats& stats, Runtime::Loader& runtime,
                            Runtime::RandomGenerator& random);
  ~ZoneAwareLoadBalancerBase();

  /**
   * Pick the host list to use, doing zone aware routing when the hosts are sufficiently healthy.
   */
  const HostVector& hostsToUse();

private:
  enum class LocalityRoutingState {
    // Locality based routing is off.
    NoLocalityRouting,
    // All queries can be routed to the local locality.
    LocalityDirect,
    // The local locality can not handle the anticipated load. Residual load will be spread across
    // various other localities.
    LocalityResidual
  };

  /**
   * Increase per_priority_state_ to at least priority_set.hostSetsPerPriority().size()
   */
  void resizePerPriorityState();

  /**
   * @return decision on quick exit from locality aware routing based on cluster configuration.
   * This gets recalculated on update callback.
   */
  bool earlyExitNonLocalityRouting();

  /**
   * Try to select upstream hosts from the same locality.
   * @param host_set the last host set returned by chooseHostSet()
   */
  const HostVector& tryChooseLocalLocalityHosts(const HostSet& host_set);

  /**
   * @return (number of hosts in a given locality)/(total number of hosts) in ret param.
   * The result is stored as integer number and scaled by 10000 multiplier for better precision.
   * Caller is responsible for allocation/de-allocation of ret.
   */
  void calculateLocalityPercentage(const HostsPerLocality& hosts_per_locality, uint64_t* ret);

  /**
   * Regenerate locality aware routing structures for fast decisions on upstream locality selection.
   */
  void regenerateLocalityRoutingStructures();

  HostSet& localHostSet() const { return *local_priority_set_->hostSetsPerPriority()[0]; }

  // The set of local Envoy instances which are load balancing across priority_set_.
  const PrioritySet* local_priority_set_;

  struct PerPriorityState {
    // The percent of requests which can be routed to the local locality.
    uint64_t local_percent_to_route_{};
    // Tracks the current state of locality based routing.
    LocalityRoutingState locality_routing_state_{LocalityRoutingState::NoLocalityRouting};
    // When locality_routing_state_ == LocalityResidual this tracks the capacity
    // for each of the non-local localities to determine what traffic should be
    // routed where.
    std::vector<uint64_t> residual_capacity_;
  };
  typedef std::unique_ptr<PerPriorityState> PerPriorityStatePtr;
  // Routing state broken out for each priority level in priority_set_.
  std::vector<PerPriorityStatePtr> per_priority_state_;
  Common::CallbackHandle* local_priority_set_member_update_cb_handle_{};
};

/**
 * Implementation of LoadBalancer that performs RR selection across the hosts in the cluster.
 */
class RoundRobinLoadBalancer : public LoadBalancer, ZoneAwareLoadBalancerBase {
public:
  RoundRobinLoadBalancer(const PrioritySet& priority_set, const PrioritySet* local_priority_set,
                         ClusterStats& stats, Runtime::Loader& runtime,
                         Runtime::RandomGenerator& random)
      : ZoneAwareLoadBalancerBase(priority_set, local_priority_set, stats, runtime, random) {}

  // Upstream::LoadBalancer
  HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;

private:
  size_t rr_index_{};
};

/**
 * Weighted Least Request load balancer.
 *
 * In a normal setup when all hosts have the same weight of 1 it randomly picks up two healthy hosts
 * and compares number of active requests.
 * Technique is based on http://www.eecs.harvard.edu/~michaelm/postscripts/mythesis.pdf
 *
 * When any of the hosts have non 1 weight, apply random weighted balancing.
 * Randomly pickup the host and send 'weight' number of requests to it.
 * This technique is acceptable for load testing but
 * will not work well in situations where requests take a long time.
 * In that case a different algorithm using a full scan will be required.
 */
class LeastRequestLoadBalancer : public LoadBalancer, ZoneAwareLoadBalancerBase {
public:
  LeastRequestLoadBalancer(const PrioritySet& priority_set, const PrioritySet* local_priority_set,
                           ClusterStats& stats, Runtime::Loader& runtime,
                           Runtime::RandomGenerator& random);

  // Upstream::LoadBalancer
  HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;

private:
  HostSharedPtr last_host_;
  uint32_t hits_left_{};
};

/**
 * Random load balancer that picks a random host out of all hosts.
 */
class RandomLoadBalancer : public LoadBalancer, ZoneAwareLoadBalancerBase {
public:
  RandomLoadBalancer(const PrioritySet& priority_set, const PrioritySet* local_priority_set,
                     ClusterStats& stats, Runtime::Loader& runtime,
                     Runtime::RandomGenerator& random)
      : ZoneAwareLoadBalancerBase(priority_set, local_priority_set, stats, runtime, random) {}

  // Upstream::LoadBalancer
  HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;
};

/**
 * Implementation of LoadBalancerSubsetInfo.
 */
class LoadBalancerSubsetInfoImpl : public LoadBalancerSubsetInfo {
public:
  LoadBalancerSubsetInfoImpl(const envoy::api::v2::Cluster::LbSubsetConfig& subset_config)
      : enabled_(!subset_config.subset_selectors().empty()),
        fallback_policy_(subset_config.fallback_policy()),
        default_subset_(subset_config.default_subset()) {
    for (const auto& subset : subset_config.subset_selectors()) {
      if (!subset.keys().empty()) {
        subset_keys_.emplace_back(
            std::set<std::string>(subset.keys().begin(), subset.keys().end()));
      }
    }
  }

  // Upstream::LoadBalancerSubsetInfo
  bool isEnabled() const override { return enabled_; }
  envoy::api::v2::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy fallbackPolicy() const override {
    return fallback_policy_;
  }
  const ProtobufWkt::Struct& defaultSubset() const override { return default_subset_; }
  const std::vector<std::set<std::string>>& subsetKeys() const override { return subset_keys_; }

private:
  const bool enabled_;
  const envoy::api::v2::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy fallback_policy_;
  const ProtobufWkt::Struct default_subset_;
  std::vector<std::set<std::string>> subset_keys_;
};

} // namespace Upstream
} // namespace Envoy
