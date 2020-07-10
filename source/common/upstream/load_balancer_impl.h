#pragma once

#include <cstdint>
#include <queue>
#include <set>
#include <vector>

#include "envoy/common/random_generator.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "common/protobuf/utility.h"
#include "common/upstream/edf_scheduler.h"

namespace Envoy {
namespace Upstream {

// Priority levels and localities are considered overprovisioned with this factor.
static constexpr uint32_t kDefaultOverProvisioningFactor = 140;

/**
 * Base class for all LB implementations.
 */
class LoadBalancerBase : public LoadBalancer {
public:
  enum class HostAvailability { Healthy, Degraded };

  // A utility function to chose a priority level based on a precomputed hash and
  // two PriorityLoad vectors, one for healthy load and one for degraded.
  //
  // Returns the priority, a number between 0 and per_priority_load.size()-1 as well as which host
  // availability level was chosen.
  static std::pair<uint32_t, HostAvailability>
  choosePriority(uint64_t hash, const HealthyLoad& healthy_per_priority_load,
                 const DegradedLoad& degraded_per_priority_load);

  HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;

protected:
  /**
   * By implementing this method instead of chooseHost, host selection will
   * be subject to host filters specified by LoadBalancerContext.
   *
   * Host selection will be retried up to the number specified by
   * hostSelectionRetryCount on LoadBalancerContext, and if no hosts are found
   * within the allowed attempts, the host that was selected during the last
   * attempt will be returned.
   *
   * If host selection is idempotent (i.e. retrying will not change the outcome),
   * sub classes should override chooseHost to avoid the unnecessary overhead of
   * retrying host selection.
   */
  virtual HostConstSharedPtr chooseHostOnce(LoadBalancerContext* context) PURE;

  /**
   * For the given host_set @return if we should be in a panic mode or not. For example, if the
   * majority of hosts are unhealthy we'll be likely in a panic mode. In this case we'll route
   * requests to hosts regardless of whether they are healthy or not.
   */
  bool isHostSetInPanic(const HostSet& host_set);

  /**
   * Method is called when all host sets are in panic mode.
   * In such state the load is distributed based on the number of hosts
   * in given priority regardless of their health.
   */
  void recalculateLoadInTotalPanic();

  LoadBalancerBase(const PrioritySet& priority_set, ClusterStats& stats, Runtime::Loader& runtime,
                   Random::RandomGenerator& random,
                   const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config);

  // Choose host set randomly, based on the healthy_per_priority_load_ and
  // degraded_per_priority_load_. per_priority_load_ is consulted first, spilling over to
  // degraded_per_priority_load_ if necessary. When a host set is selected based on
  // degraded_per_priority_load_, only degraded hosts should be selected from that host set.
  //
  // @return host set to use and which availability to target.
  std::pair<HostSet&, HostAvailability> chooseHostSet(LoadBalancerContext* context);

  uint32_t percentageLoad(uint32_t priority) const {
    return per_priority_load_.healthy_priority_load_.get()[priority];
  }
  uint32_t percentageDegradedLoad(uint32_t priority) const {
    return per_priority_load_.degraded_priority_load_.get()[priority];
  }
  bool isInPanic(uint32_t priority) const { return per_priority_panic_[priority]; }

  ClusterStats& stats_;
  Runtime::Loader& runtime_;
  Random::RandomGenerator& random_;
  const uint32_t default_healthy_panic_percent_;
  // The priority-ordered set of hosts to use for load balancing.
  const PrioritySet& priority_set_;

public:
  // Called when a host set at the given priority level is updated. This updates
  // per_priority_health for that priority level, and may update per_priority_load for all
  // priority levels.
  void static recalculatePerPriorityState(uint32_t priority, const PrioritySet& priority_set,
                                          HealthyAndDegradedLoad& priority_load,
                                          HealthyAvailability& per_priority_health,
                                          DegradedAvailability& per_priority_degraded);
  void recalculatePerPriorityPanic();

protected:
  // Method calculates normalized total availability.
  //
  // The availability of a priority is ratio of available (healthy/degraded) hosts over the total
  // number of hosts multiplied by 100 and the overprovisioning factor. The total availability is
  // the sum of the availability of each priority, up to a maximum of 100.
  //
  // For example, using the default overprovisioning factor of 1.4, a if priority A has 4 hosts,
  // of which 1 is degraded and 1 is healthy, it will have availability of 2/4 * 100 * 1.4 = 70.
  //
  // Assuming two priorities with availability 60 and 70, the total availability would be 100.
  static uint32_t
  calculateNormalizedTotalAvailability(HealthyAvailability& per_priority_health,
                                       DegradedAvailability& per_priority_degraded) {
    const auto health =
        std::accumulate(per_priority_health.get().begin(), per_priority_health.get().end(), 0);
    const auto degraded =
        std::accumulate(per_priority_degraded.get().begin(), per_priority_degraded.get().end(), 0);

    return std::min<uint32_t>(health + degraded, 100);
  }
  // The percentage load (0-100) for each priority level when targeting healthy hosts and
  // the percentage load (0-100) for each priority level when targeting degraded hosts.
  HealthyAndDegradedLoad per_priority_load_;
  // The health percentage (0-100) for each priority level.
  HealthyAvailability per_priority_health_;
  // The degraded percentage (0-100) for each priority level.
  DegradedAvailability per_priority_degraded_;
  // Levels which are in panic
  std::vector<bool> per_priority_panic_;
};

class LoadBalancerContextBase : public LoadBalancerContext {
public:
  absl::optional<uint64_t> computeHashKey() override { return {}; }

  const Network::Connection* downstreamConnection() const override { return nullptr; }

  const Router::MetadataMatchCriteria* metadataMatchCriteria() override { return nullptr; }

  const Http::RequestHeaderMap* downstreamHeaders() const override { return nullptr; }

  const HealthyAndDegradedLoad&
  determinePriorityLoad(const PrioritySet&, const HealthyAndDegradedLoad& original_priority_load,
                        const Upstream::RetryPriority::PriorityMappingFunc&) override {
    return original_priority_load;
  }

  bool shouldSelectAnotherHost(const Host&) override { return false; }

  uint32_t hostSelectionRetryCount() const override { return 1; }

  Network::Socket::OptionsSharedPtr upstreamSocketOptions() const override { return nullptr; }

  Network::TransportSocketOptionsSharedPtr upstreamTransportSocketOptions() const override {
    return nullptr;
  }
};

/**
 * Base class for zone aware load balancers
 */
class ZoneAwareLoadBalancerBase : public LoadBalancerBase {
protected:
  // Both priority_set and local_priority_set if non-null must have at least one host set.
  ZoneAwareLoadBalancerBase(
      const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterStats& stats,
      Runtime::Loader& runtime, Random::RandomGenerator& random,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config);
  ~ZoneAwareLoadBalancerBase() override;

  // When deciding which hosts to use on an LB decision, we need to know how to index into the
  // priority_set. This priority_set cursor is used by ZoneAwareLoadBalancerBase subclasses, e.g.
  // RoundRobinLoadBalancer, to index into auxiliary data structures specific to the LB for
  // a given host set selection.
  struct HostsSource {
    enum class SourceType {
      // All hosts in the host set.
      AllHosts,
      // All healthy hosts in the host set.
      HealthyHosts,
      // All degraded hosts in the host set.
      DegradedHosts,
      // Healthy hosts for locality @ locality_index.
      LocalityHealthyHosts,
      // Degraded hosts for locality @ locality_index.
      LocalityDegradedHosts,
    };

    HostsSource() = default;

    HostsSource(uint32_t priority, SourceType source_type)
        : priority_(priority), source_type_(source_type) {
      ASSERT(source_type == SourceType::AllHosts || source_type == SourceType::HealthyHosts ||
             source_type == SourceType::DegradedHosts);
    }

    HostsSource(uint32_t priority, SourceType source_type, uint32_t locality_index)
        : priority_(priority), source_type_(source_type), locality_index_(locality_index) {
      ASSERT(source_type == SourceType::LocalityHealthyHosts ||
             source_type == SourceType::LocalityDegradedHosts);
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
   * If no host is chosen (due to fail_traffic_on_panic being set), return absl::nullopt.
   */
  absl::optional<HostsSource> hostSourceToUse(LoadBalancerContext* context);

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
   * @return (number of hosts in a given locality)/(total number of hosts) in `ret` param.
   * The result is stored as integer number and scaled by 10000 multiplier for better precision.
   * Caller is responsible for allocation/de-allocation of `ret`.
   */
  void calculateLocalityPercentage(const HostsPerLocality& hosts_per_locality, uint64_t* ret);

  /**
   * Regenerate locality aware routing structures for fast decisions on upstream locality selection.
   */
  void regenerateLocalityRoutingStructures();

  HostSet& localHostSet() const { return *local_priority_set_->hostSetsPerPriority()[0]; }

  static HostsSource::SourceType localitySourceType(HostAvailability host_availability) {
    switch (host_availability) {
    case HostAvailability::Healthy:
      return HostsSource::SourceType::LocalityHealthyHosts;
    case HostAvailability::Degraded:
      return HostsSource::SourceType::LocalityDegradedHosts;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  static HostsSource::SourceType sourceType(HostAvailability host_availability) {
    switch (host_availability) {
    case HostAvailability::Healthy:
      return HostsSource::SourceType::HealthyHosts;
    case HostAvailability::Degraded:
      return HostsSource::SourceType::DegradedHosts;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  // The set of local Envoy instances which are load balancing across priority_set_.
  const PrioritySet* local_priority_set_;

  const uint32_t routing_enabled_;
  const uint64_t min_cluster_size_;
  const bool fail_traffic_on_panic_;

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
  using PerPriorityStatePtr = std::unique_ptr<PerPriorityState>;
  // Routing state broken out for each priority level in priority_set_.
  std::vector<PerPriorityStatePtr> per_priority_state_;
  Common::CallbackHandle* local_priority_set_member_update_cb_handle_{};
};

/**
 * Base implementation of LoadBalancer that performs weighted RR selection across the hosts in the
 * cluster. This scheduler respects host weighting and utilizes an EdfScheduler to achieve O(log
 * n) pick and insertion time complexity, O(n) memory use. The key insight is that if we schedule
 * with 1 / weight deadline, we will achieve the desired pick frequency for weighted RR in a given
 * interval. Naive implementations of weighted RR are either O(n) pick time or O(m * n) memory use,
 * where m is the weight range. We also explicitly check for the unweighted special case and use a
 * simple index to achieve O(1) scheduling in that case.
 * TODO(htuch): We use EDF at Google, but the EDF scheduler may be overkill if we don't want to
 * support large ranges of weights or arbitrary precision floating weights, we could construct an
 * explicit schedule, since m will be a small constant factor in O(m * n). This
 * could also be done on a thread aware LB, avoiding creating multiple EDF
 * instances.
 *
 * This base class also supports unweighted selection which derived classes can use to customize
 * behavior. Derived classes can also override how host weight is determined when in weighted mode.
 */
class EdfLoadBalancerBase : public ZoneAwareLoadBalancerBase {
public:
  EdfLoadBalancerBase(const PrioritySet& priority_set, const PrioritySet* local_priority_set,
                      ClusterStats& stats, Runtime::Loader& runtime,
                      Random::RandomGenerator& random,
                      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config);

  // Upstream::LoadBalancerBase
  HostConstSharedPtr chooseHostOnce(LoadBalancerContext* context) override;

protected:
  struct Scheduler {
    // EdfScheduler for weighted LB. The edf_ is only created when the original
    // host weights of 2 or more hosts differ. When not present, the
    // implementation of chooseHostOnce falls back to unweightedHostPick.
    std::unique_ptr<EdfScheduler<const Host>> edf_;
  };

  void initialize();

  // Seed to allow us to desynchronize load balancers across a fleet. If we don't
  // do this, multiple Envoys that receive an update at the same time (or even
  // multiple load balancers on the same host) will send requests to
  // backends in roughly lock step, causing significant imbalance and potential
  // overload.
  const uint64_t seed_;

private:
  void refresh(uint32_t priority);
  virtual void refreshHostSource(const HostsSource& source) PURE;
  virtual double hostWeight(const Host& host) PURE;
  virtual HostConstSharedPtr unweightedHostPick(const HostVector& hosts_to_use,
                                                const HostsSource& source) PURE;

  // Scheduler for each valid HostsSource.
  std::unordered_map<HostsSource, Scheduler, HostsSourceHash> scheduler_;
};

/**
 * A round robin load balancer. When in weighted mode, EDF scheduling is used. When in not
 * weighted mode, simple RR index selection is used.
 */
class RoundRobinLoadBalancer : public EdfLoadBalancerBase {
public:
  RoundRobinLoadBalancer(const PrioritySet& priority_set, const PrioritySet* local_priority_set,
                         ClusterStats& stats, Runtime::Loader& runtime,
                         Random::RandomGenerator& random,
                         const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
      : EdfLoadBalancerBase(priority_set, local_priority_set, stats, runtime, random,
                            common_config) {
    initialize();
  }

private:
  void refreshHostSource(const HostsSource& source) override {
    // insert() is used here on purpose so that we don't overwrite the index if the host source
    // already exists. Note that host sources will never be removed, but given how uncommon this
    // is it probably doesn't matter.
    rr_indexes_.insert({source, seed_});
  }
  double hostWeight(const Host& host) override { return host.weight(); }
  HostConstSharedPtr unweightedHostPick(const HostVector& hosts_to_use,
                                        const HostsSource& source) override {
    // To avoid storing the RR index in the base class, we end up using a second map here with
    // host source as the key. This means that each LB decision will require two map lookups in
    // the unweighted case. We might consider trying to optimize this in the future.
    ASSERT(rr_indexes_.find(source) != rr_indexes_.end());
    return hosts_to_use[rr_indexes_[source]++ % hosts_to_use.size()];
  }

  std::unordered_map<HostsSource, uint64_t, HostsSourceHash> rr_indexes_;
};

/**
 * Weighted Least Request load balancer.
 *
 * In a normal setup when all hosts have the same weight it randomly picks up N healthy hosts
 * (where N is specified in the LB configuration) and compares number of active requests. Technique
 * is based on http://www.eecs.harvard.edu/~michaelm/postscripts/mythesis.pdf and is known as P2C
 * (power of two choices).
 *
 * When hosts have different weights, an RR EDF schedule is used. Host weight is scaled
 * by the number of active requests at pick/insert time. Thus, hosts will never fully drain as
 * they would in normal P2C, though they will get picked less and less often. In the future, we
 * can consider two alternate algorithms:
 * 1) Expand out all hosts by weight (using more memory) and do standard P2C.
 * 2) Use a weighted Maglev table, and perform P2C on two random hosts selected from the table.
 *    The benefit of the Maglev table is at the expense of resolution, memory usage is capped.
 *    Additionally, the Maglev table can be shared amongst all threads.
 */
class LeastRequestLoadBalancer : public EdfLoadBalancerBase {
public:
  LeastRequestLoadBalancer(
      const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterStats& stats,
      Runtime::Loader& runtime, Random::RandomGenerator& random,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config,
      const absl::optional<envoy::config::cluster::v3::Cluster::LeastRequestLbConfig>
          least_request_config)
      : EdfLoadBalancerBase(priority_set, local_priority_set, stats, runtime, random,
                            common_config),
        choice_count_(
            least_request_config.has_value()
                ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(least_request_config.value(), choice_count, 2)
                : 2) {
    initialize();
  }

private:
  void refreshHostSource(const HostsSource&) override {}
  double hostWeight(const Host& host) override {
    // Here we scale host weight by the number of active requests at the time we do the pick. We
    // always add 1 to avoid division by 0. It might be possible to do better by picking two hosts
    // off of the schedule, and selecting the one with fewer active requests at the time of
    // selection.
    // TODO(mattklein123): @htuch brings up the point that how we are scaling weight here might not
    // be the only/best way of doing this. Essentially, it makes weight and active requests equally
    // important. Are they equally important in practice? There is no right answer here and we might
    // want to iterate on this as we gain more experience.
    return static_cast<double>(host.weight()) / (host.stats().rq_active_.value() + 1);
  }
  HostConstSharedPtr unweightedHostPick(const HostVector& hosts_to_use,
                                        const HostsSource& source) override;
  const uint32_t choice_count_;
};

/**
 * Random load balancer that picks a random host out of all hosts.
 */
class RandomLoadBalancer : public ZoneAwareLoadBalancerBase {
public:
  RandomLoadBalancer(const PrioritySet& priority_set, const PrioritySet* local_priority_set,
                     ClusterStats& stats, Runtime::Loader& runtime, Random::RandomGenerator& random,
                     const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
      : ZoneAwareLoadBalancerBase(priority_set, local_priority_set, stats, runtime, random,
                                  common_config) {}

  // Upstream::LoadBalancerBase
  HostConstSharedPtr chooseHostOnce(LoadBalancerContext* context) override;
};

/**
 * Implementation of SubsetSelector
 */
class SubsetSelectorImpl : public SubsetSelector {
public:
  SubsetSelectorImpl(const Protobuf::RepeatedPtrField<std::string>& selector_keys,
                     envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::
                         LbSubsetSelectorFallbackPolicy fallback_policy,
                     const Protobuf::RepeatedPtrField<std::string>& fallback_keys_subset);

  // SubsetSelector
  const std::set<std::string>& selectorKeys() const override { return selector_keys_; }
  envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::
      LbSubsetSelectorFallbackPolicy
      fallbackPolicy() const override {
    return fallback_policy_;
  }
  const std::set<std::string>& fallbackKeysSubset() const override { return fallback_keys_subset_; }

private:
  const std::set<std::string> selector_keys_;
  const envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::
      LbSubsetSelectorFallbackPolicy fallback_policy_;
  const std::set<std::string> fallback_keys_subset_;
};

/**
 * Implementation of LoadBalancerSubsetInfo.
 */
class LoadBalancerSubsetInfoImpl : public LoadBalancerSubsetInfo {
public:
  LoadBalancerSubsetInfoImpl(
      const envoy::config::cluster::v3::Cluster::LbSubsetConfig& subset_config)
      : enabled_(!subset_config.subset_selectors().empty()),
        fallback_policy_(subset_config.fallback_policy()),
        default_subset_(subset_config.default_subset()),
        locality_weight_aware_(subset_config.locality_weight_aware()),
        scale_locality_weight_(subset_config.scale_locality_weight()),
        panic_mode_any_(subset_config.panic_mode_any()), list_as_any_(subset_config.list_as_any()) {
    for (const auto& subset : subset_config.subset_selectors()) {
      if (!subset.keys().empty()) {
        subset_selectors_.emplace_back(std::make_shared<SubsetSelectorImpl>(
            subset.keys(), subset.fallback_policy(), subset.fallback_keys_subset()));
      }
    }
  }

  // Upstream::LoadBalancerSubsetInfo
  bool isEnabled() const override { return enabled_; }
  envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy
  fallbackPolicy() const override {
    return fallback_policy_;
  }
  const ProtobufWkt::Struct& defaultSubset() const override { return default_subset_; }
  const std::vector<SubsetSelectorPtr>& subsetSelectors() const override {
    return subset_selectors_;
  }
  bool localityWeightAware() const override { return locality_weight_aware_; }
  bool scaleLocalityWeight() const override { return scale_locality_weight_; }
  bool panicModeAny() const override { return panic_mode_any_; }
  bool listAsAny() const override { return list_as_any_; }

private:
  const bool enabled_;
  const envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy
      fallback_policy_;
  const ProtobufWkt::Struct default_subset_;
  std::vector<SubsetSelectorPtr> subset_selectors_;
  const bool locality_weight_aware_;
  const bool scale_locality_weight_;
  const bool panic_mode_any_;
  const bool list_as_any_;
};

} // namespace Upstream
} // namespace Envoy
