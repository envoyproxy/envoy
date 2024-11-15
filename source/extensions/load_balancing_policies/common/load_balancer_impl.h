#pragma once

#include <bitset>
#include <cmath>
#include <cstdint>
#include <memory>
#include <queue>
#include <set>
#include <vector>

#include "envoy/common/callback.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/load_balancing_policies/least_request/v3/least_request.pb.h"
#include "envoy/extensions/load_balancing_policies/random/v3/random.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/common/upstream/edf_scheduler.h"
#include "source/common/upstream/load_balancer_context_base.h"

namespace Envoy {
namespace Upstream {

inline bool tooManyPreconnects(size_t num_preconnect_picks, uint32_t healthy_hosts) {
  // Currently we only allow the number of preconnected connections to equal the
  // number of healthy hosts.
  return num_preconnect_picks >= healthy_hosts;
}

// Distributes load between priorities based on the per priority availability and the normalized
// total availability. Load is assigned to each priority according to how available each priority is
// adjusted for the normalized total availability.
//
// @param per_priority_load vector of loads that should be populated.
// @param per_priority_availability the percentage availability of each priority, used to determine
// how much load each priority can handle.
// @param total_load the amount of load that may be distributed. Will be updated with the amount of
// load remaining after distribution.
// @param normalized_total_availability the total availability, up to a max of 100. Used to
// scale the load when the total availability is less than 100%.
// @return the first available priority and the remaining load
std::pair<int32_t, size_t> distributeLoad(PriorityLoad& per_priority_load,
                                          const PriorityAvailability& per_priority_availability,
                                          size_t total_load, size_t normalized_total_availability);

class LoadBalancerConfigHelper {
public:
  template <class LegacyProto>
  static absl::optional<envoy::extensions::load_balancing_policies::common::v3::SlowStartConfig>
  slowStartConfigFromLegacyProto(const LegacyProto& proto_config) {
    if (!proto_config.has_slow_start_config()) {
      return {};
    }

    envoy::extensions::load_balancing_policies::common::v3::SlowStartConfig slow_start_config;
    const auto& legacy_slow_start_config = proto_config.slow_start_config();
    if (legacy_slow_start_config.has_slow_start_window()) {
      *slow_start_config.mutable_slow_start_window() = legacy_slow_start_config.slow_start_window();
    }
    if (legacy_slow_start_config.has_aggression()) {
      *slow_start_config.mutable_aggression() = legacy_slow_start_config.aggression();
    }
    if (legacy_slow_start_config.has_min_weight_percent()) {
      *slow_start_config.mutable_min_weight_percent() =
          legacy_slow_start_config.min_weight_percent();
    }
    return slow_start_config;
  }

  template <class Proto>
  static absl::optional<envoy::extensions::load_balancing_policies::common::v3::SlowStartConfig>
  slowStartConfigFromProto(const Proto& proto_config) {
    if (!proto_config.has_slow_start_config()) {
      return {};
    }
    return proto_config.slow_start_config();
  }

  static absl::optional<envoy::extensions::load_balancing_policies::common::v3::LocalityLbConfig>
  localityLbConfigFromCommonLbConfig(
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config);

  template <class Proto>
  static absl::optional<envoy::extensions::load_balancing_policies::common::v3::LocalityLbConfig>
  localityLbConfigFromProto(const Proto& proto_config) {
    if (!proto_config.has_locality_lb_config()) {
      return {};
    }
    return proto_config.locality_lb_config();
  }
};

/**
 * Base class for all LB implementations.
 */
class LoadBalancerBase : public LoadBalancer, protected Logger::Loggable<Logger::Id::upstream> {
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

  // Pool selection not implemented.
  absl::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext* /*context*/,
                           const Upstream::Host& /*host*/,
                           std::vector<uint8_t>& /*hash_key*/) override {
    return absl::nullopt;
  }
  // Lifetime tracking not implemented.
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
    return {};
  }

protected:
  /**
   * For the given host_set @return if we should be in a panic mode or not. For example, if the
   * majority of hosts are unhealthy we'll be likely in a panic mode. In this case we'll route
   * requests to hosts regardless of whether they are healthy or not.
   */
  bool isHostSetInPanic(const HostSet& host_set) const;

  /**
   * Method is called when all host sets are in panic mode.
   * In such state the load is distributed based on the number of hosts
   * in given priority regardless of their health.
   */
  void recalculateLoadInTotalPanic();

  LoadBalancerBase(const PrioritySet& priority_set, ClusterLbStats& stats, Runtime::Loader& runtime,
                   Random::RandomGenerator& random, uint32_t healthy_panic_threshold);

  // Choose host set randomly, based on the healthy_per_priority_load_ and
  // degraded_per_priority_load_. per_priority_load_ is consulted first, spilling over to
  // degraded_per_priority_load_ if necessary. When a host set is selected based on
  // degraded_per_priority_load_, only degraded hosts should be selected from that host set.
  //
  // @return host set to use and which availability to target.
  std::pair<HostSet&, HostAvailability> chooseHostSet(LoadBalancerContext* context,
                                                      uint64_t hash) const;

  uint32_t percentageLoad(uint32_t priority) const {
    return per_priority_load_.healthy_priority_load_.get()[priority];
  }
  uint32_t percentageDegradedLoad(uint32_t priority) const {
    return per_priority_load_.degraded_priority_load_.get()[priority];
  }
  bool isInPanic(uint32_t priority) const { return per_priority_panic_[priority]; }
  uint64_t random(bool peeking) {
    if (peeking) {
      stashed_random_.push_back(random_.random());
      return stashed_random_.back();
    } else {
      if (!stashed_random_.empty()) {
        auto random = stashed_random_.front();
        stashed_random_.pop_front();
        return random;
      } else {
        return random_.random();
      }
    }
  }

  ClusterLbStats& stats_;
  Runtime::Loader& runtime_;
  std::deque<uint64_t> stashed_random_;
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
                                          DegradedAvailability& per_priority_degraded,
                                          uint32_t& total_healthy_hosts);
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
  // The total count of healthy hosts across all priority levels.
  uint32_t total_healthy_hosts_;

private:
  Common::CallbackHandlePtr priority_update_cb_;
};

/**
 * Base class for zone aware load balancers
 */
class ZoneAwareLoadBalancerBase : public LoadBalancerBase {
public:
  using LocalityLbConfig = envoy::extensions::load_balancing_policies::common::v3::LocalityLbConfig;

  HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;

protected:
  // Both priority_set and local_priority_set if non-null must have at least one host set.
  ZoneAwareLoadBalancerBase(const PrioritySet& priority_set, const PrioritySet* local_priority_set,
                            ClusterLbStats& stats, Runtime::Loader& runtime,
                            Random::RandomGenerator& random, uint32_t healthy_panic_threshold,
                            const absl::optional<LocalityLbConfig> locality_config);

  // When deciding which hosts to use on an LB decision, we need to know how to index into the
  // priority_set. This priority_set cursor is used by ZoneAwareLoadBalancerBase subclasses, e.g.
  // RoundRobinLoadBalancer, to index into auxiliary data structures specific to the LB for
  // a given host set selection.
  struct HostsSource {
    enum class SourceType : uint8_t {
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

    // TODO(kbaichoo): plumb the priority parameter as uint8_t all the way from
    // the config.
    HostsSource(uint32_t priority, SourceType source_type)
        : priority_(static_cast<uint8_t>(priority)), source_type_(source_type) {
      ASSERT(priority <= 128, "Priority out of bounds.");
      ASSERT(source_type == SourceType::AllHosts || source_type == SourceType::HealthyHosts ||
             source_type == SourceType::DegradedHosts);
    }

    // TODO(kbaichoo): plumb the priority parameter as uint8_t all the way from
    // the config.
    HostsSource(uint32_t priority, SourceType source_type, uint32_t locality_index)
        : priority_(static_cast<uint8_t>(priority)), source_type_(source_type),
          locality_index_(locality_index) {
      ASSERT(priority <= 128, "Priority out of bounds.");
      ASSERT(source_type == SourceType::LocalityHealthyHosts ||
             source_type == SourceType::LocalityDegradedHosts);
    }

    // Priority in PrioritySet.
    uint8_t priority_{};

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
      // This is only used for absl::flat_hash_map keys, so we don't need a deterministic hash.
      size_t hash = std::hash<uint32_t>()(hs.priority_);
      hash = 37 * hash + std::hash<size_t>()(static_cast<std::size_t>(hs.source_type_));
      hash = 37 * hash + std::hash<uint32_t>()(hs.locality_index_);
      return hash;
    }
  };

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
   * Pick the host source to use, doing zone aware routing when the hosts are sufficiently healthy.
   * If no host is chosen (due to fail_traffic_on_panic being set), return absl::nullopt.
   */
  absl::optional<HostsSource> hostSourceToUse(LoadBalancerContext* context, uint64_t hash) const;

  /**
   * Index into priority_set via hosts source descriptor.
   */
  const HostVector& hostSourceToHosts(HostsSource hosts_source) const;

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

  struct LocalityPercentages {
    // The percentage of local hosts in a specific locality.
    // Percentage is stored as integer number and scaled by 10000 multiplier for better precision.
    // If upstream_percentage is 0, local_percentage may not be representative
    // of the actual percentage and will be set to 0.
    uint64_t local_percentage;
    // The percentage of upstream hosts in a specific locality.
    // Percentage is stored as integer number and scaled by 10000 multiplier for better precision.
    uint64_t upstream_percentage;
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
  uint32_t tryChooseLocalLocalityHosts(const HostSet& host_set) const;

  /**
   * @return combined per-locality information about percentages of local/upstream hosts in each
   * upstream locality. See LocalityPercentages for more details. The ordering of localities
   * matches the ordering of upstream localities in the input upstream_hosts_per_locality.
   */
  absl::FixedArray<LocalityPercentages>
  calculateLocalityPercentages(const HostsPerLocality& local_hosts_per_locality,
                               const HostsPerLocality& upstream_hosts_per_locality);

  /**
   * Regenerate locality aware routing structures for fast decisions on upstream locality selection.
   */
  void regenerateLocalityRoutingStructures();

  HostSet& localHostSet() const { return *local_priority_set_->hostSetsPerPriority()[0]; }

  static absl::optional<HostsSource::SourceType>
  localitySourceType(HostAvailability host_availability) {
    switch (host_availability) {
    case HostAvailability::Healthy:
      return absl::make_optional<HostsSource::SourceType>(
          HostsSource::SourceType::LocalityHealthyHosts);
    case HostAvailability::Degraded:
      return absl::make_optional<HostsSource::SourceType>(
          HostsSource::SourceType::LocalityDegradedHosts);
    }
    IS_ENVOY_BUG("unexpected locality source type enum");
    return absl::nullopt;
  }

  static absl::optional<HostsSource::SourceType> sourceType(HostAvailability host_availability) {
    switch (host_availability) {
    case HostAvailability::Healthy:
      return absl::make_optional<HostsSource::SourceType>(HostsSource::SourceType::HealthyHosts);
    case HostAvailability::Degraded:
      return absl::make_optional<HostsSource::SourceType>(HostsSource::SourceType::DegradedHosts);
    }

    IS_ENVOY_BUG("unexpected source type enum");
    return absl::nullopt;
  }

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
  using PerPriorityStatePtr = std::unique_ptr<PerPriorityState>;
  // Routing state broken out for each priority level in priority_set_.
  std::vector<PerPriorityStatePtr> per_priority_state_;
  Common::CallbackHandlePtr priority_update_cb_;
  Common::CallbackHandlePtr local_priority_set_member_update_cb_handle_;

  // Config for zone aware routing.
  const uint64_t min_cluster_size_;
  // Keep small members (bools and enums) at the end of class, to reduce alignment overhead.
  const uint32_t routing_enabled_;
  const bool fail_traffic_on_panic_ : 1;

  // If locality weight aware routing is enabled.
  const bool locality_weighted_balancing_ : 1;

  friend class TestZoneAwareLoadBalancer;
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
  using SlowStartConfig = envoy::extensions::load_balancing_policies::common::v3::SlowStartConfig;

  EdfLoadBalancerBase(const PrioritySet& priority_set, const PrioritySet* local_priority_set,
                      ClusterLbStats& stats, Runtime::Loader& runtime,
                      Random::RandomGenerator& random, uint32_t healthy_panic_threshold,
                      const absl::optional<LocalityLbConfig> locality_config,
                      const absl::optional<SlowStartConfig> slow_start_config,
                      TimeSource& time_source);

  // Upstream::ZoneAwareLoadBalancerBase
  HostConstSharedPtr peekAnotherHost(LoadBalancerContext* context) override;
  HostConstSharedPtr chooseHostOnce(LoadBalancerContext* context) override;

protected:
  struct Scheduler {
    // EdfScheduler for weighted LB. The edf_ is only created when the original
    // host weights of 2 or more hosts differ. When not present, the
    // implementation of chooseHostOnce falls back to unweightedHostPick.
    std::unique_ptr<EdfScheduler<Host>> edf_;
  };

  void initialize();

  virtual void refresh(uint32_t priority);

  // Return `true` if refresh() should always use EDF scheduler, even if host
  // weights are all equal. Default to `false`.
  virtual bool alwaysUseEdfScheduler() const;

  bool isSlowStartEnabled() const;
  bool noHostsAreInSlowStart() const;

  virtual void recalculateHostsInSlowStart(const HostVector& hosts_added);

  // Seed to allow us to desynchronize load balancers across a fleet. If we don't
  // do this, multiple Envoys that receive an update at the same time (or even
  // multiple load balancers on the same host) will send requests to
  // backends in roughly lock step, causing significant imbalance and potential
  // overload.
  const uint64_t seed_;

  double applySlowStartFactor(double host_weight, const Host& host) const;

private:
  friend class EdfLoadBalancerBasePeer;
  virtual void refreshHostSource(const HostsSource& source) PURE;
  virtual double hostWeight(const Host& host) const PURE;
  virtual HostConstSharedPtr unweightedHostPeek(const HostVector& hosts_to_use,
                                                const HostsSource& source) PURE;
  virtual HostConstSharedPtr unweightedHostPick(const HostVector& hosts_to_use,
                                                const HostsSource& source) PURE;

  // Scheduler for each valid HostsSource.
  absl::flat_hash_map<HostsSource, Scheduler, HostsSourceHash> scheduler_;
  Common::CallbackHandlePtr priority_update_cb_;
  Common::CallbackHandlePtr member_update_cb_;

protected:
  // Slow start related config
  const std::chrono::milliseconds slow_start_window_;
  const absl::optional<Runtime::Double> aggression_runtime_;
  TimeSource& time_source_;
  MonotonicTime latest_host_added_time_;
  const double slow_start_min_weight_percent_;
};

} // namespace Upstream
} // namespace Envoy
