#pragma once

#include <cstdint>
#include <queue>
#include <set>
#include <vector>

#include "envoy/api/v2/cds.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "common/upstream/edf_scheduler.h"

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
  bool isGlobalPanic(const HostSet& host_set);

  LoadBalancerBase(const PrioritySet& priority_set, ClusterStats& stats, Runtime::Loader& runtime,
                   Runtime::RandomGenerator& random,
                   const envoy::api::v2::Cluster::CommonLbConfig& common_config);

  // Choose host set randomly, based on the per_priority_load_;
  const HostSet& chooseHostSet();

  uint32_t percentageLoad(uint32_t priority) const { return per_priority_load_[priority]; }

  ClusterStats& stats_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  const uint32_t default_healthy_panic_percent_;
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
                            Runtime::RandomGenerator& random,
                            const envoy::api::v2::Cluster::CommonLbConfig& common_config);
  ~ZoneAwareLoadBalancerBase();

  // When deciding which hosts to use on an LB decision, we need to know how to index into the
  // priority_set. This priority_set cursor is used by ZoneAwareLoadBalancerBase subclasses, e.g.
  // RoundRobinLoadBalancer, to index into auxillary data structures specific to the LB for
  // a given host set selection.
  struct HostsSource {
    enum class SourceType {
      // All hosts in the host set.
      AllHosts,
      // All healthy hosts in the host set.
      HealthyHosts,
      // Healthy hosts for locality @ locality_index.
      LocalityHealthyHosts,
    };

    HostsSource() {}

    HostsSource(uint32_t priority, SourceType source_type)
        : priority_(priority), source_type_(source_type) {
      ASSERT(source_type == SourceType::AllHosts || source_type == SourceType::HealthyHosts);
    }

    HostsSource(uint32_t priority, SourceType source_type, uint32_t locality_index)
        : priority_(priority), source_type_(source_type), locality_index_(locality_index) {
      ASSERT(source_type == SourceType::LocalityHealthyHosts);
    }

    // Priority in PrioritySet.
    uint32_t priority_{};

    // How to index into HostSet for a given priority.
    SourceType source_type_{};

    // Locality index into HostsPerLocality for SourceType::LocalityHealthyHosts.
    uint32_t locality_index_{};

    bool operator==(const HostsSource& other) const {
      return priority_ == other.priority_ && source_type_ == other.source_type_ &&
             locality_index_ == other.locality_index_;
    }
  };

  struct HostsSourceHash {
    size_t operator()(const HostsSource& hs) const {
      // This is only used for std::unordered_map keys, so we don't need a deterministic hash.
      size_t hash = std::hash<uint32_t>()(hs.priority_);
      hash = 37 * hash + std::hash<size_t>()(static_cast<std::size_t>(hs.source_type_));
      hash = 37 * hash + std::hash<uint32_t>()(hs.locality_index_);
      return hash;
    }
  };

  /**
   * Pick the host source to use, doing zone aware routing when the hosts are sufficiently healthy.
   */
  HostsSource hostSourceToUse();

  /**
   * Index into priority_set via hosts source descriptor.
   */
  const HostVector& hostSourceToHosts(HostsSource hosts_source);

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
  uint32_t tryChooseLocalLocalityHosts(const HostSet& host_set);

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
 * This scheduler respects host weighting and utilizes an EdfScheduler to achieve O(log n)
 * pick and insertion time complexity, O(n) memory use. The key insight is that if we schedule with
 * 1 / weight deadline, we will achieve the desired pick frequency for weighted RR in a given
 * interval. Naive implementations of weighted RR are either O(n) pick time or O(m * n) memory use,
 * where m is the weight range. We also explicitly check for the unweighted special case and use a
 * simple index to acheive O(1) scheduling in that case.
 * TODO(htuch): We use EDF at Google, but the EDF scheduler may be overkill if we don't want to
 * support large ranges of weights or arbitrary precision floating weights, we could construct an
 * explicit schedule, since m will be a small constant factor in O(m * n).
 */
class RoundRobinLoadBalancer : public LoadBalancer, ZoneAwareLoadBalancerBase {
public:
  RoundRobinLoadBalancer(const PrioritySet& priority_set, const PrioritySet* local_priority_set,
                         ClusterStats& stats, Runtime::Loader& runtime,
                         Runtime::RandomGenerator& random,
                         const envoy::api::v2::Cluster::CommonLbConfig& common_config);

  // Upstream::LoadBalancer
  HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;

private:
  void refresh(uint32_t priority);

  struct Scheduler {
    // EdfScheduler for weighted RR.
    EdfScheduler<const Host> edf_;
    // Simple clock hand for when we do unweighted.
    size_t rr_index_{};
    bool weighted_{};
  };

  // Scheduler for each valid HostsSource.
  std::unordered_map<HostsSource, Scheduler, HostsSourceHash> scheduler_;
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
                           Runtime::RandomGenerator& random,
                           const envoy::api::v2::Cluster::CommonLbConfig& common_config);

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
                     Runtime::RandomGenerator& random,
                     const envoy::api::v2::Cluster::CommonLbConfig& common_config)
      : ZoneAwareLoadBalancerBase(priority_set, local_priority_set, stats, runtime, random,
                                  common_config) {}

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
