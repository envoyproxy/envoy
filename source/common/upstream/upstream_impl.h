#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/api/v2/endpoint/endpoint.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/event/timer.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/dns.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/locality.h"
#include "envoy/upstream/upstream.h"

#include "common/common/callback_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/init/manager_impl.h"
#include "common/network/utility.h"
#include "common/stats/isolated_store_impl.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/outlier_detection_impl.h"
#include "common/upstream/resource_manager_impl.h"
#include "common/upstream/transport_socket_match_impl.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Upstream {

/**
 * Null implementation of HealthCheckHostMonitor.
 */
class HealthCheckHostMonitorNullImpl : public HealthCheckHostMonitor {
public:
  // Upstream::HealthCheckHostMonitor
  void setUnhealthy() override {}
};

/**
 * Implementation of Upstream::HostDescription.
 */
class HostDescriptionImpl : virtual public HostDescription,
                            protected Logger::Loggable<Logger::Id::upstream> {
public:
  HostDescriptionImpl(
      ClusterInfoConstSharedPtr cluster, const std::string& hostname,
      Network::Address::InstanceConstSharedPtr dest_address,
      const envoy::api::v2::core::Metadata& metadata,
      const envoy::api::v2::core::Locality& locality,
      const envoy::api::v2::endpoint::Endpoint::HealthCheckConfig& health_check_config,
      uint32_t priority);

  Network::TransportSocketFactory& transportSocketFactory() const override {
    return socket_factory_;
  }

  bool canary() const override { return canary_; }
  void canary(bool is_canary) override { canary_ = is_canary; }

  // Metadata getter/setter.
  //
  // It's possible that the lock that guards the metadata will become highly contended (e.g.:
  // endpoints churning during a deploy of a large cluster). A possible improvement
  // would be to use TLS and post metadata updates from the main thread. This model would
  // possibly benefit other related and expensive computations too (e.g.: updating subsets).
  //
  // TODO(rgs1): we should move to absl locks, once there's support for R/W locks. We should
  // also add lock annotations, once they work correctly with R/W locks.
  const std::shared_ptr<envoy::api::v2::core::Metadata> metadata() const override {
    absl::ReaderMutexLock lock(&metadata_mutex_);
    return metadata_;
  }
  void metadata(const envoy::api::v2::core::Metadata& new_metadata) override {
    absl::WriterMutexLock lock(&metadata_mutex_);
    metadata_ = std::make_shared<envoy::api::v2::core::Metadata>(new_metadata);
  }

  const ClusterInfo& cluster() const override { return *cluster_; }
  HealthCheckHostMonitor& healthChecker() const override {
    if (health_checker_) {
      return *health_checker_;
    } else {
      static HealthCheckHostMonitorNullImpl* null_health_checker =
          new HealthCheckHostMonitorNullImpl();
      return *null_health_checker;
    }
  }
  Outlier::DetectorHostMonitor& outlierDetector() const override {
    if (outlier_detector_) {
      return *outlier_detector_;
    } else {
      static Outlier::DetectorHostMonitorNullImpl* null_outlier_detector =
          new Outlier::DetectorHostMonitorNullImpl();
      return *null_outlier_detector;
    }
  }
  HostStats& stats() const override { return stats_; }
  const std::string& hostname() const override { return hostname_; }
  Network::Address::InstanceConstSharedPtr address() const override { return address_; }
  Network::Address::InstanceConstSharedPtr healthCheckAddress() const override {
    return health_check_address_;
  }
  const envoy::api::v2::core::Locality& locality() const override { return locality_; }
  Stats::StatName localityZoneStatName() const override {
    return locality_zone_stat_name_.statName();
  }
  uint32_t priority() const override { return priority_; }
  void priority(uint32_t priority) override { priority_ = priority; }

private:
  Network::TransportSocketFactory&
  resolveTransportSocketFactory(const Network::Address::InstanceConstSharedPtr& dest_address,
                                const envoy::api::v2::core::Metadata& metadata);

protected:
  ClusterInfoConstSharedPtr cluster_;
  const std::string hostname_;
  Network::Address::InstanceConstSharedPtr address_;
  Network::Address::InstanceConstSharedPtr health_check_address_;
  std::atomic<bool> canary_;
  mutable absl::Mutex metadata_mutex_;
  std::shared_ptr<envoy::api::v2::core::Metadata> metadata_ ABSL_GUARDED_BY(metadata_mutex_);
  const envoy::api::v2::core::Locality locality_;
  Stats::StatNameManagedStorage locality_zone_stat_name_;
  mutable HostStats stats_;
  Outlier::DetectorHostMonitorPtr outlier_detector_;
  HealthCheckHostMonitorPtr health_checker_;
  std::atomic<uint32_t> priority_;
  Network::TransportSocketFactory& socket_factory_;
};

/**
 * Implementation of Upstream::Host.
 */
class HostImpl : public HostDescriptionImpl,
                 public Host,
                 public std::enable_shared_from_this<HostImpl> {
public:
  HostImpl(ClusterInfoConstSharedPtr cluster, const std::string& hostname,
           Network::Address::InstanceConstSharedPtr address,
           const envoy::api::v2::core::Metadata& metadata, uint32_t initial_weight,
           const envoy::api::v2::core::Locality& locality,
           const envoy::api::v2::endpoint::Endpoint::HealthCheckConfig& health_check_config,
           uint32_t priority, const envoy::api::v2::core::HealthStatus health_status)
      : HostDescriptionImpl(cluster, hostname, address, metadata, locality, health_check_config,
                            priority),
        used_(true) {
    setEdsHealthFlag(health_status);
    weight(initial_weight);
  }

  // Upstream::Host
  std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>>
  counters() const override {
    return stats_.counters();
  }
  CreateConnectionData createConnection(
      Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
      Network::TransportSocketOptionsSharedPtr transport_socket_options) const override;
  CreateConnectionData createHealthCheckConnection(Event::Dispatcher& dispatcher) const override;

  std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>>
  gauges() const override {
    return stats_.gauges();
  }
  void healthFlagClear(HealthFlag flag) override { health_flags_ &= ~enumToInt(flag); }
  bool healthFlagGet(HealthFlag flag) const override { return health_flags_ & enumToInt(flag); }
  void healthFlagSet(HealthFlag flag) override { health_flags_ |= enumToInt(flag); }

  ActiveHealthFailureType getActiveHealthFailureType() const override {
    return active_health_failure_type_;
  }
  void setActiveHealthFailureType(ActiveHealthFailureType type) override {
    active_health_failure_type_ = type;
  }

  void setHealthChecker(HealthCheckHostMonitorPtr&& health_checker) override {
    health_checker_ = std::move(health_checker);
  }
  void setOutlierDetector(Outlier::DetectorHostMonitorPtr&& outlier_detector) override {
    outlier_detector_ = std::move(outlier_detector);
  }
  Host::Health health() const override {
    // If any of the unhealthy flags are set, host is unhealthy.
    if (healthFlagGet(HealthFlag::FAILED_ACTIVE_HC) ||
        healthFlagGet(HealthFlag::FAILED_OUTLIER_CHECK) ||
        healthFlagGet(HealthFlag::FAILED_EDS_HEALTH)) {
      return Host::Health::Unhealthy;
    }

    // If any of the degraded flags are set, host is degraded.
    if (healthFlagGet(HealthFlag::DEGRADED_ACTIVE_HC) ||
        healthFlagGet(HealthFlag::DEGRADED_EDS_HEALTH)) {
      return Host::Health::Degraded;
    }

    // The host must have no flags or be pending removal.
    ASSERT(health_flags_ == 0 || healthFlagGet(HealthFlag::PENDING_DYNAMIC_REMOVAL));
    return Host::Health::Healthy;
  }

  uint32_t weight() const override { return weight_; }
  void weight(uint32_t new_weight) override;
  bool used() const override { return used_; }
  void used(bool new_used) override { used_ = new_used; }

protected:
  static Network::ClientConnectionPtr
  createConnection(Event::Dispatcher& dispatcher, const ClusterInfo& cluster,
                   const Network::Address::InstanceConstSharedPtr& address,
                   Network::TransportSocketFactory& socket_factory,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   Network::TransportSocketOptionsSharedPtr transport_socket_options);

private:
  void setEdsHealthFlag(envoy::api::v2::core::HealthStatus health_status);

  std::atomic<uint64_t> health_flags_{};
  ActiveHealthFailureType active_health_failure_type_{};
  std::atomic<uint32_t> weight_;
  std::atomic<bool> used_;
};

class HostsPerLocalityImpl : public HostsPerLocality {
public:
  HostsPerLocalityImpl() : HostsPerLocalityImpl(std::vector<HostVector>(), false) {}

  // Single locality constructor
  HostsPerLocalityImpl(const HostVector& hosts, bool has_local_locality = false)
      : HostsPerLocalityImpl(std::vector<HostVector>({hosts}), has_local_locality) {}

  HostsPerLocalityImpl(std::vector<HostVector>&& locality_hosts, bool has_local_locality)
      : local_(has_local_locality), hosts_per_locality_(std::move(locality_hosts)) {
    ASSERT(!has_local_locality || !hosts_per_locality_.empty());
  }

  bool hasLocalLocality() const override { return local_; }
  const std::vector<HostVector>& get() const override { return hosts_per_locality_; }
  std::vector<HostsPerLocalityConstSharedPtr>
  filter(const std::vector<std::function<bool(const Host&)>>& predicate) const override;

  // The const shared pointer for the empty HostsPerLocalityImpl.
  static HostsPerLocalityConstSharedPtr empty() {
    static HostsPerLocalityConstSharedPtr empty = std::make_shared<HostsPerLocalityImpl>();
    return empty;
  }

private:
  // Does an entry exist for the local locality?
  bool local_{};
  // The first entry is for local hosts in the local locality.
  std::vector<HostVector> hosts_per_locality_;
};

/**
 * A class for management of the set of hosts for a given priority level.
 */
class HostSetImpl : public HostSet {
public:
  HostSetImpl(uint32_t priority, absl::optional<uint32_t> overprovisioning_factor)
      : priority_(priority), overprovisioning_factor_(overprovisioning_factor.has_value()
                                                          ? overprovisioning_factor.value()
                                                          : kDefaultOverProvisioningFactor),
        hosts_(new HostVector()), healthy_hosts_(new HealthyHostVector()),
        degraded_hosts_(new DegradedHostVector()), excluded_hosts_(new ExcludedHostVector()) {}

  /**
   * Install a callback that will be invoked when the host set membership changes.
   * @param callback supplies the callback to invoke.
   * @return Common::CallbackHandle* the callback handle.
   */
  Common::CallbackHandle* addPriorityUpdateCb(PrioritySet::PriorityUpdateCb callback) const {
    return member_update_cb_helper_.add(callback);
  }

  // Upstream::HostSet
  const HostVector& hosts() const override { return *hosts_; }
  HostVectorConstSharedPtr hostsPtr() const override { return hosts_; }
  const HostVector& healthyHosts() const override { return healthy_hosts_->get(); }
  HealthyHostVectorConstSharedPtr healthyHostsPtr() const override { return healthy_hosts_; }
  const HostVector& degradedHosts() const override { return degraded_hosts_->get(); }
  DegradedHostVectorConstSharedPtr degradedHostsPtr() const override { return degraded_hosts_; }
  const HostVector& excludedHosts() const override { return excluded_hosts_->get(); }
  ExcludedHostVectorConstSharedPtr excludedHostsPtr() const override { return excluded_hosts_; }
  const HostsPerLocality& hostsPerLocality() const override { return *hosts_per_locality_; }
  HostsPerLocalityConstSharedPtr hostsPerLocalityPtr() const override {
    return hosts_per_locality_;
  }
  const HostsPerLocality& healthyHostsPerLocality() const override {
    return *healthy_hosts_per_locality_;
  }
  HostsPerLocalityConstSharedPtr healthyHostsPerLocalityPtr() const override {
    return healthy_hosts_per_locality_;
  }
  const HostsPerLocality& degradedHostsPerLocality() const override {
    return *degraded_hosts_per_locality_;
  }
  HostsPerLocalityConstSharedPtr degradedHostsPerLocalityPtr() const override {
    return degraded_hosts_per_locality_;
  }
  const HostsPerLocality& excludedHostsPerLocality() const override {
    return *excluded_hosts_per_locality_;
  }
  HostsPerLocalityConstSharedPtr excludedHostsPerLocalityPtr() const override {
    return excluded_hosts_per_locality_;
  }
  LocalityWeightsConstSharedPtr localityWeights() const override { return locality_weights_; }
  absl::optional<uint32_t> chooseHealthyLocality() override;
  absl::optional<uint32_t> chooseDegradedLocality() override;
  uint32_t priority() const override { return priority_; }
  uint32_t overprovisioningFactor() const override { return overprovisioning_factor_; }

  static PrioritySet::UpdateHostsParams
  updateHostsParams(HostVectorConstSharedPtr hosts,
                    HostsPerLocalityConstSharedPtr hosts_per_locality,
                    HealthyHostVectorConstSharedPtr healthy_hosts,
                    HostsPerLocalityConstSharedPtr healthy_hosts_per_locality,
                    DegradedHostVectorConstSharedPtr degraded_hosts,
                    HostsPerLocalityConstSharedPtr degraded_hosts_per_locality,
                    ExcludedHostVectorConstSharedPtr excluded_hosts,
                    HostsPerLocalityConstSharedPtr excluded_hosts_per_locality);
  static PrioritySet::UpdateHostsParams updateHostsParams(const HostSet& host_set);
  static PrioritySet::UpdateHostsParams
  partitionHosts(HostVectorConstSharedPtr hosts, HostsPerLocalityConstSharedPtr hosts_per_locality);

  void updateHosts(PrioritySet::UpdateHostsParams&& update_hosts_params,
                   LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
                   const HostVector& hosts_removed,
                   absl::optional<uint32_t> overprovisioning_factor = absl::nullopt);

protected:
  virtual void runUpdateCallbacks(const HostVector& hosts_added, const HostVector& hosts_removed) {
    member_update_cb_helper_.runCallbacks(priority_, hosts_added, hosts_removed);
  }

private:
  // Weight for a locality taking into account health status using the provided eligible hosts per
  // locality.
  static double effectiveLocalityWeight(uint32_t index,
                                        const HostsPerLocality& eligible_hosts_per_locality,
                                        const HostsPerLocality& excluded_hosts_per_locality,
                                        const HostsPerLocality& all_hosts_per_locality,
                                        const LocalityWeights& locality_weights,
                                        uint32_t overprovisioning_factor);

  uint32_t priority_;
  uint32_t overprovisioning_factor_;
  HostVectorConstSharedPtr hosts_;
  HealthyHostVectorConstSharedPtr healthy_hosts_;
  DegradedHostVectorConstSharedPtr degraded_hosts_;
  ExcludedHostVectorConstSharedPtr excluded_hosts_;
  HostsPerLocalityConstSharedPtr hosts_per_locality_{HostsPerLocalityImpl::empty()};
  HostsPerLocalityConstSharedPtr healthy_hosts_per_locality_{HostsPerLocalityImpl::empty()};
  HostsPerLocalityConstSharedPtr degraded_hosts_per_locality_{HostsPerLocalityImpl::empty()};
  HostsPerLocalityConstSharedPtr excluded_hosts_per_locality_{HostsPerLocalityImpl::empty()};
  // TODO(mattklein123): Remove mutable.
  mutable Common::CallbackManager<uint32_t, const HostVector&, const HostVector&>
      member_update_cb_helper_;
  // Locality weights (used to build WRR locality_scheduler_);
  LocalityWeightsConstSharedPtr locality_weights_;
  // WRR locality scheduler state.
  struct LocalityEntry {
    LocalityEntry(uint32_t index, double effective_weight)
        : index_(index), effective_weight_(effective_weight) {}
    const uint32_t index_;
    const double effective_weight_;
  };

  // Rebuilds the provided locality scheduler with locality entries based on the locality weights
  // and eligible hosts.
  //
  // @param locality_scheduler the locality scheduler to rebuild. Will be set to nullptr if no
  // localities are eligible.
  // @param locality_entries the vector that holds locality entries. Will be reset and populated
  // with entries corresponding to the new scheduler.
  // @param eligible_hosts_per_locality eligible hosts for this scheduler grouped by locality.
  // @param eligible_hosts all eligible hosts for this scheduler.
  // @param all_hosts_per_locality all hosts for this HostSet grouped by locality.
  // @param locality_weights the weighting of each locality.
  // @param overprovisioning_factor the overprovisioning factor to use when computing the effective
  // weight of a locality.
  static void rebuildLocalityScheduler(
      std::unique_ptr<EdfScheduler<LocalityEntry>>& locality_scheduler,
      std::vector<std::shared_ptr<LocalityEntry>>& locality_entries,
      const HostsPerLocality& eligible_hosts_per_locality, const HostVector& eligible_hosts,
      HostsPerLocalityConstSharedPtr all_hosts_per_locality,
      HostsPerLocalityConstSharedPtr excluded_hosts_per_locality,
      LocalityWeightsConstSharedPtr locality_weights, uint32_t overprovisioning_factor);

  static absl::optional<uint32_t> chooseLocality(EdfScheduler<LocalityEntry>* locality_scheduler);

  std::vector<std::shared_ptr<LocalityEntry>> healthy_locality_entries_;
  std::unique_ptr<EdfScheduler<LocalityEntry>> healthy_locality_scheduler_;
  std::vector<std::shared_ptr<LocalityEntry>> degraded_locality_entries_;
  std::unique_ptr<EdfScheduler<LocalityEntry>> degraded_locality_scheduler_;
};

using HostSetImplPtr = std::unique_ptr<HostSetImpl>;

/**
 * A class for management of the set of hosts in a given cluster.
 */
class PrioritySetImpl : public PrioritySet {
public:
  PrioritySetImpl() : batch_update_(false) {}
  // From PrioritySet
  Common::CallbackHandle* addMemberUpdateCb(MemberUpdateCb callback) const override {
    return member_update_cb_helper_.add(callback);
  }
  Common::CallbackHandle* addPriorityUpdateCb(PriorityUpdateCb callback) const override {
    return priority_update_cb_helper_.add(callback);
  }
  const std::vector<std::unique_ptr<HostSet>>& hostSetsPerPriority() const override {
    return host_sets_;
  }
  // Get the host set for this priority level, creating it if necessary.
  const HostSet&
  getOrCreateHostSet(uint32_t priority,
                     absl::optional<uint32_t> overprovisioning_factor = absl::nullopt);

  void updateHosts(uint32_t priority, UpdateHostsParams&& update_hosts_params,
                   LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
                   const HostVector& hosts_removed,
                   absl::optional<uint32_t> overprovisioning_factor = absl::nullopt) override;

  void batchHostUpdate(BatchUpdateCb& callback) override;

protected:
  // Allows subclasses of PrioritySetImpl to create their own type of HostSetImpl.
  virtual HostSetImplPtr createHostSet(uint32_t priority,
                                       absl::optional<uint32_t> overprovisioning_factor) {
    return std::make_unique<HostSetImpl>(priority, overprovisioning_factor);
  }

protected:
  virtual void runUpdateCallbacks(const HostVector& hosts_added, const HostVector& hosts_removed) {
    member_update_cb_helper_.runCallbacks(hosts_added, hosts_removed);
  }
  virtual void runReferenceUpdateCallbacks(uint32_t priority, const HostVector& hosts_added,
                                           const HostVector& hosts_removed) {
    priority_update_cb_helper_.runCallbacks(priority, hosts_added, hosts_removed);
  }
  // This vector will generally have at least one member, for priority level 0.
  // It will expand as host sets are added but currently does not shrink to
  // avoid any potential lifetime issues.
  std::vector<std::unique_ptr<HostSet>> host_sets_;

private:
  // TODO(mattklein123): Remove mutable.
  mutable Common::CallbackManager<const HostVector&, const HostVector&> member_update_cb_helper_;
  mutable Common::CallbackManager<uint32_t, const HostVector&, const HostVector&>
      priority_update_cb_helper_;
  bool batch_update_ : 1;

  // Helper class to maintain state as we perform multiple host updates. Keeps track of all hosts
  // that have been added/removed throughout the batch update, and ensures that we properly manage
  // the batch_update_ flag.
  class BatchUpdateScope : public HostUpdateCb {
  public:
    explicit BatchUpdateScope(PrioritySetImpl& parent) : parent_(parent) {
      ASSERT(!parent_.batch_update_);
      parent_.batch_update_ = true;
    }
    ~BatchUpdateScope() override { parent_.batch_update_ = false; }

    void updateHosts(uint32_t priority, PrioritySet::UpdateHostsParams&& update_hosts_params,
                     LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
                     const HostVector& hosts_removed,
                     absl::optional<uint32_t> overprovisioning_factor) override;

    std::unordered_set<HostSharedPtr> all_hosts_added_;
    std::unordered_set<HostSharedPtr> all_hosts_removed_;

  private:
    PrioritySetImpl& parent_;
    std::unordered_set<uint32_t> priorities_;
  };
};

/**
 * Implementation of ClusterInfo that reads from JSON.
 */
class ClusterInfoImpl : public ClusterInfo, protected Logger::Loggable<Logger::Id::upstream> {
public:
  ClusterInfoImpl(const envoy::api::v2::Cluster& config,
                  const envoy::api::v2::core::BindConfig& bind_config, Runtime::Loader& runtime,
                  TransportSocketMatcherPtr&& socket_matcher, Stats::ScopePtr&& stats_scope,
                  bool added_via_api, ProtobufMessage::ValidationVisitor& validation_visitor,
                  Server::Configuration::TransportSocketFactoryContext&);

  static ClusterStats generateStats(Stats::Scope& scope);
  static ClusterLoadReportStats generateLoadReportStats(Stats::Scope& scope);
  static ClusterCircuitBreakersStats generateCircuitBreakersStats(Stats::Scope& scope,
                                                                  const std::string& stat_prefix,
                                                                  bool track_remaining);

  // Upstream::ClusterInfo
  bool addedViaApi() const override { return added_via_api_; }
  const envoy::api::v2::Cluster::CommonLbConfig& lbConfig() const override {
    return common_lb_config_;
  }
  std::chrono::milliseconds connectTimeout() const override { return connect_timeout_; }
  const absl::optional<std::chrono::milliseconds> idleTimeout() const override {
    return idle_timeout_;
  }
  uint32_t perConnectionBufferLimitBytes() const override {
    return per_connection_buffer_limit_bytes_;
  }
  uint64_t features() const override { return features_; }
  const Http::Http1Settings& http1Settings() const override { return http1_settings_; }
  const Http::Http2Settings& http2Settings() const override { return http2_settings_; }
  ProtocolOptionsConfigConstSharedPtr
  extensionProtocolOptions(const std::string& name) const override;
  LoadBalancerType lbType() const override { return lb_type_; }
  envoy::api::v2::Cluster::DiscoveryType type() const override { return type_; }
  const absl::optional<envoy::api::v2::Cluster::CustomClusterType>& clusterType() const override {
    return cluster_type_;
  }
  const absl::optional<envoy::api::v2::Cluster::LeastRequestLbConfig>&
  lbLeastRequestConfig() const override {
    return lb_least_request_config_;
  }
  const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>&
  lbRingHashConfig() const override {
    return lb_ring_hash_config_;
  }
  const absl::optional<envoy::api::v2::Cluster::OriginalDstLbConfig>&
  lbOriginalDstConfig() const override {
    return lb_original_dst_config_;
  }
  bool maintenanceMode() const override;
  uint64_t maxRequestsPerConnection() const override { return max_requests_per_connection_; }
  uint32_t maxResponseHeadersCount() const override { return max_response_headers_count_; }
  const std::string& name() const override { return name_; }
  ResourceManager& resourceManager(ResourcePriority priority) const override;
  TransportSocketMatcher& transportSocketMatcher() const override { return *socket_matcher_; }
  ClusterStats& stats() const override { return stats_; }
  Stats::Scope& statsScope() const override { return *stats_scope_; }
  ClusterLoadReportStats& loadReportStats() const override { return load_report_stats_; }
  const Network::Address::InstanceConstSharedPtr& sourceAddress() const override {
    return source_address_;
  };
  const LoadBalancerSubsetInfo& lbSubsetInfo() const override { return lb_subset_; }
  const envoy::api::v2::core::Metadata& metadata() const override { return metadata_; }
  const Envoy::Config::TypedMetadata& typedMetadata() const override { return typed_metadata_; }

  const Network::ConnectionSocket::OptionsSharedPtr& clusterSocketOptions() const override {
    return cluster_socket_options_;
  };

  bool drainConnectionsOnHostRemoval() const override { return drain_connections_on_host_removal_; }
  bool warmHosts() const override { return warm_hosts_; }

  absl::optional<std::string> eds_service_name() const override { return eds_service_name_; }

  void createNetworkFilterChain(Network::Connection&) const override;
  Http::Protocol
  upstreamHttpProtocol(absl::optional<Http::Protocol> downstream_protocol) const override;

private:
  struct ResourceManagers {
    ResourceManagers(const envoy::api::v2::Cluster& config, Runtime::Loader& runtime,
                     const std::string& cluster_name, Stats::Scope& stats_scope);
    ResourceManagerImplPtr load(const envoy::api::v2::Cluster& config, Runtime::Loader& runtime,
                                const std::string& cluster_name, Stats::Scope& stats_scope,
                                const envoy::api::v2::core::RoutingPriority& priority);

    using Managers = std::array<ResourceManagerImplPtr, NumResourcePriorities>;

    Managers managers_;
  };

  Runtime::Loader& runtime_;
  const std::string name_;
  const envoy::api::v2::Cluster::DiscoveryType type_;
  const uint64_t max_requests_per_connection_;
  const uint32_t max_response_headers_count_;
  const std::chrono::milliseconds connect_timeout_;
  absl::optional<std::chrono::milliseconds> idle_timeout_;
  const uint32_t per_connection_buffer_limit_bytes_;
  TransportSocketMatcherPtr socket_matcher_;
  Stats::ScopePtr stats_scope_;
  mutable ClusterStats stats_;
  Stats::IsolatedStoreImpl load_report_stats_store_;
  mutable ClusterLoadReportStats load_report_stats_;
  const uint64_t features_;
  const Http::Http1Settings http1_settings_;
  const Http::Http2Settings http2_settings_;
  const std::map<std::string, ProtocolOptionsConfigConstSharedPtr> extension_protocol_options_;
  mutable ResourceManagers resource_managers_;
  const std::string maintenance_mode_runtime_key_;
  const Network::Address::InstanceConstSharedPtr source_address_;
  LoadBalancerType lb_type_;
  absl::optional<envoy::api::v2::Cluster::LeastRequestLbConfig> lb_least_request_config_;
  absl::optional<envoy::api::v2::Cluster::RingHashLbConfig> lb_ring_hash_config_;
  absl::optional<envoy::api::v2::Cluster::OriginalDstLbConfig> lb_original_dst_config_;
  const bool added_via_api_;
  LoadBalancerSubsetInfoImpl lb_subset_;
  const envoy::api::v2::core::Metadata metadata_;
  Envoy::Config::TypedMetadataImpl<ClusterTypedMetadataFactory> typed_metadata_;
  const envoy::api::v2::Cluster::CommonLbConfig common_lb_config_;
  const Network::ConnectionSocket::OptionsSharedPtr cluster_socket_options_;
  const bool drain_connections_on_host_removal_;
  const bool warm_hosts_;
  absl::optional<std::string> eds_service_name_;
  const absl::optional<envoy::api::v2::Cluster::CustomClusterType> cluster_type_;
  const std::unique_ptr<Server::Configuration::CommonFactoryContext> factory_context_;
  std::vector<Network::FilterFactoryCb> filter_factories_;
};

/**
 * Function that creates a Network::TransportSocketFactoryPtr
 * given a cluster configuration and transport socket factory
 * context.
 */
Network::TransportSocketFactoryPtr
createTransportSocketFactory(const envoy::api::v2::Cluster& config,
                             Server::Configuration::TransportSocketFactoryContext& factory_context);

/**
 * Base class all primary clusters.
 */
class ClusterImplBase : public Cluster, protected Logger::Loggable<Logger::Id::upstream> {

public:
  // Upstream::Cluster
  PrioritySet& prioritySet() override { return priority_set_; }
  const PrioritySet& prioritySet() const override { return priority_set_; }

  /**
   * Optionally set the health checker for the primary cluster. This is done after cluster
   * creation since the health checker assumes that the cluster has already been fully initialized
   * so there is a cyclic dependency. However we want the cluster to own the health checker.
   */
  void setHealthChecker(const HealthCheckerSharedPtr& health_checker);

  /**
   * Optionally set the outlier detector for the primary cluster. Done for the same reason as
   * documented in setHealthChecker().
   */
  void setOutlierDetector(const Outlier::DetectorSharedPtr& outlier_detector);

  /**
   * Wrapper around Network::Address::resolveProtoAddress() that provides improved error message
   * based on the cluster's type.
   * @param address supplies the address proto to resolve.
   * @return Network::Address::InstanceConstSharedPtr the resolved address.
   */
  const Network::Address::InstanceConstSharedPtr
  resolveProtoAddress(const envoy::api::v2::core::Address& address);

  // Partitions the provided list of hosts into three new lists containing the healthy, degraded
  // and excluded hosts respectively.
  static std::tuple<HealthyHostVectorConstSharedPtr, DegradedHostVectorConstSharedPtr,
                    ExcludedHostVectorConstSharedPtr>
  partitionHostList(const HostVector& hosts);

  // Partitions the provided list of hosts per locality into three new lists containing the healthy,
  // degraded and excluded hosts respectively.
  static std::tuple<HostsPerLocalityConstSharedPtr, HostsPerLocalityConstSharedPtr,
                    HostsPerLocalityConstSharedPtr>
  partitionHostsPerLocality(const HostsPerLocality& hosts);
  Stats::SymbolTable& symbolTable() { return symbol_table_; }

  // Upstream::Cluster
  HealthChecker* healthChecker() override { return health_checker_.get(); }
  ClusterInfoConstSharedPtr info() const override { return info_; }
  Outlier::Detector* outlierDetector() override { return outlier_detector_.get(); }
  const Outlier::Detector* outlierDetector() const override { return outlier_detector_.get(); }
  void initialize(std::function<void()> callback) override;

protected:
  ClusterImplBase(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                  Server::Configuration::TransportSocketFactoryContext& factory_context,
                  Stats::ScopePtr&& stats_scope, bool added_via_api);

  /**
   * Overridden by every concrete cluster. The cluster should do whatever pre-init is needed. E.g.,
   * query DNS, contact EDS, etc.
   */
  virtual void startPreInit() PURE;

  /**
   * Called by every concrete cluster when pre-init is complete. At this point,
   * shared init starts init_manager_ initialization and determines if there
   * is an initial health check pass needed, etc.
   */
  void onPreInitComplete();

  /**
   * Called by every concrete cluster after all targets registered at init manager are
   * initialized. At this point, shared init takes over and determines if there is an initial health
   * check pass needed, etc.
   */
  void onInitDone();

  virtual void reloadHealthyHostsHelper(const HostSharedPtr& host);

  // This init manager is shared via TransportSocketFactoryContext. The initialization targets that
  // register with this init manager are expected to be for implementations of SdsApi (see
  // SdsApi::init_target_).
  Init::ManagerImpl init_manager_;

  // Once all targets are initialized (i.e. once all dynamic secrets are loaded), this watcher calls
  // onInitDone() above.
  Init::WatcherImpl init_watcher_;

  Runtime::Loader& runtime_;
  ClusterInfoConstSharedPtr info_; // This cluster info stores the stats scope so it must be
                                   // initialized first and destroyed last.
  HealthCheckerSharedPtr health_checker_;
  Outlier::DetectorSharedPtr outlier_detector_;

protected:
  PrioritySetImpl priority_set_;

private:
  void finishInitialization();
  void reloadHealthyHosts(const HostSharedPtr& host);

  bool initialization_started_{};
  std::function<void()> initialization_complete_callback_;
  uint64_t pending_initialize_health_checks_{};
  Stats::SymbolTable& symbol_table_;
};

using ClusterImplBaseSharedPtr = std::shared_ptr<ClusterImplBase>;

/**
 * Manages PriorityState of a cluster. PriorityState is a per-priority binding of a set of hosts
 * with its corresponding locality weight map. This is useful to store priorities/hosts/localities
 * before updating the cluster priority set.
 */
class PriorityStateManager : protected Logger::Loggable<Logger::Id::upstream> {
public:
  PriorityStateManager(ClusterImplBase& cluster, const LocalInfo::LocalInfo& local_info,
                       PrioritySet::HostUpdateCb* update_cb);

  // Initializes the PriorityState vector based on the priority specified in locality_lb_endpoint.
  void
  initializePriorityFor(const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint);

  // Registers a host based on its address to the PriorityState based on the specified priority (the
  // priority is specified by locality_lb_endpoint.priority()).
  //
  // The specified health_checker_flag is used to set the registered-host's health-flag when the
  // lb_endpoint health status is unhealthy, draining or timeout.
  void
  registerHostForPriority(const std::string& hostname,
                          Network::Address::InstanceConstSharedPtr address,
                          const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint,
                          const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint);

  void registerHostForPriority(
      const HostSharedPtr& host,
      const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint);

  void
  updateClusterPrioritySet(const uint32_t priority, HostVectorSharedPtr&& current_hosts,
                           const absl::optional<HostVector>& hosts_added,
                           const absl::optional<HostVector>& hosts_removed,
                           const absl::optional<Upstream::Host::HealthFlag> health_checker_flag,
                           absl::optional<uint32_t> overprovisioning_factor = absl::nullopt);

  // Returns the size of the current cluster priority state.
  size_t size() const { return priority_state_.size(); }

  // Returns the saved priority state.
  PriorityState& priorityState() { return priority_state_; }

private:
  ClusterImplBase& parent_;
  PriorityState priority_state_;
  const envoy::api::v2::core::Node& local_info_node_;
  PrioritySet::HostUpdateCb* update_cb_;
};

using PriorityStateManagerPtr = std::unique_ptr<PriorityStateManager>;

/**
 * Base for all dynamic cluster types.
 */
class BaseDynamicClusterImpl : public ClusterImplBase {
protected:
  using ClusterImplBase::ClusterImplBase;

  /**
   * Updates the host list of a single priority by reconciling the list of new hosts
   * with existing hosts.
   *
   * @param new_hosts the full lists of hosts in the new configuration.
   * @param current_priority_hosts the full lists of hosts for the priority to be updated. The list
   * will be modified to contain the updated list of hosts.
   * @param hosts_added_to_current_priority will be populated with hosts added to the priority.
   * @param hosts_removed_from_current_priority will be populated with hosts removed from the
   * priority.
   * @param updated_hosts is used to aggregate the new state of all hosts across priority, and will
   * be updated with the hosts that remain in this priority after the update.
   * @param all_hosts all known hosts prior to this host update.
   * @return whether the hosts for the priority changed.
   */
  bool updateDynamicHostList(const HostVector& new_hosts, HostVector& current_priority_hosts,
                             HostVector& hosts_added_to_current_priority,
                             HostVector& hosts_removed_from_current_priority,
                             HostMap& updated_hosts, const HostMap& all_hosts);
};

/**
 * Utility function to get Dns from cluster/enum.
 */
Network::DnsLookupFamily getDnsLookupFamilyFromCluster(const envoy::api::v2::Cluster& cluster);
Network::DnsLookupFamily
getDnsLookupFamilyFromEnum(envoy::api::v2::Cluster::DnsLookupFamily family);

/**
 * Utility function to report upstream cx destroy metrics
 */
void reportUpstreamCxDestroy(const Upstream::HostDescriptionConstSharedPtr& host,
                             Network::ConnectionEvent event);

/**
 * Utility function to report upstream cx destroy active request metrics
 */
void reportUpstreamCxDestroyActiveRequest(const Upstream::HostDescriptionConstSharedPtr& host,
                                          Network::ConnectionEvent event);

} // namespace Upstream
} // namespace Envoy
