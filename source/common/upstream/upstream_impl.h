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
#include "envoy/event/timer.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
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
#include "common/network/utility.h"
#include "common/stats/isolated_store_impl.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/outlier_detection_impl.h"
#include "common/upstream/resource_manager_impl.h"

#include "server/init_manager_impl.h"

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
class HostDescriptionImpl : virtual public HostDescription {
public:
  HostDescriptionImpl(
      ClusterInfoConstSharedPtr cluster, const std::string& hostname,
      Network::Address::InstanceConstSharedPtr dest_address,
      const envoy::api::v2::core::Metadata& metadata,
      const envoy::api::v2::core::Locality& locality,
      const envoy::api::v2::endpoint::Endpoint::HealthCheckConfig& health_check_config)
      : cluster_(cluster), hostname_(hostname), address_(dest_address),
        health_check_address_(health_check_config.port_value() == 0
                                  ? dest_address
                                  : Network::Utility::getAddressWithPort(
                                        *dest_address, health_check_config.port_value())),
        canary_(Config::Metadata::metadataValue(metadata, Config::MetadataFilters::get().ENVOY_LB,
                                                Config::MetadataEnvoyLbKeys::get().CANARY)
                    .bool_value()),
        metadata_(std::make_shared<envoy::api::v2::core::Metadata>(metadata)),
        locality_(locality), stats_{ALL_HOST_STATS(POOL_COUNTER(stats_store_),
                                                   POOL_GAUGE(stats_store_))} {}

  // Upstream::HostDescription
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
  virtual void metadata(const envoy::api::v2::core::Metadata& new_metadata) override {
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
  const HostStats& stats() const override { return stats_; }
  const std::string& hostname() const override { return hostname_; }
  Network::Address::InstanceConstSharedPtr address() const override { return address_; }
  Network::Address::InstanceConstSharedPtr healthCheckAddress() const override {
    return health_check_address_;
  }
  // Setting health check address is usually done at initialization. This is NOP by default.
  void setHealthCheckAddress(Network::Address::InstanceConstSharedPtr) override {}
  const envoy::api::v2::core::Locality& locality() const override { return locality_; }

protected:
  ClusterInfoConstSharedPtr cluster_;
  const std::string hostname_;
  Network::Address::InstanceConstSharedPtr address_;
  Network::Address::InstanceConstSharedPtr health_check_address_;
  std::atomic<bool> canary_;
  mutable absl::Mutex metadata_mutex_;
  std::shared_ptr<envoy::api::v2::core::Metadata> metadata_ GUARDED_BY(metadata_mutex_);
  const envoy::api::v2::core::Locality locality_;
  Stats::IsolatedStoreImpl stats_store_;
  HostStats stats_;
  Outlier::DetectorHostMonitorPtr outlier_detector_;
  HealthCheckHostMonitorPtr health_checker_;
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
           const envoy::api::v2::endpoint::Endpoint::HealthCheckConfig& health_check_config)
      : HostDescriptionImpl(cluster, hostname, address, metadata, locality, health_check_config),
        used_(true) {
    weight(initial_weight);
  }

  // Upstream::Host
  std::vector<Stats::CounterSharedPtr> counters() const override { return stats_store_.counters(); }
  CreateConnectionData
  createConnection(Event::Dispatcher& dispatcher,
                   const Network::ConnectionSocket::OptionsSharedPtr& options) const override;
  CreateConnectionData createHealthCheckConnection(Event::Dispatcher& dispatcher) const override;
  std::vector<Stats::GaugeSharedPtr> gauges() const override { return stats_store_.gauges(); }
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
  bool healthy() const override { return !health_flags_; }
  uint32_t weight() const override { return weight_; }
  void weight(uint32_t new_weight) override;
  bool used() const override { return used_; }
  void used(bool new_used) override { used_ = new_used; }

protected:
  static Network::ClientConnectionPtr
  createConnection(Event::Dispatcher& dispatcher, const ClusterInfo& cluster,
                   Network::Address::InstanceConstSharedPtr address,
                   const Network::ConnectionSocket::OptionsSharedPtr& options);

private:
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
  HostsPerLocalityConstSharedPtr filter(std::function<bool(const Host&)> predicate) const override;

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
        hosts_(new HostVector()), healthy_hosts_(new HostVector()) {}

  void updateHosts(HostVectorConstSharedPtr hosts, HostVectorConstSharedPtr healthy_hosts,
                   HostsPerLocalityConstSharedPtr hosts_per_locality,
                   HostsPerLocalityConstSharedPtr healthy_hosts_per_locality,
                   LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
                   const HostVector& hosts_removed,
                   absl::optional<uint32_t> overprovisioning_factor = absl::nullopt) override;

  /**
   * Install a callback that will be invoked when the host set membership changes.
   * @param callback supplies the callback to invoke.
   * @return Common::CallbackHandle* the callback handle.
   */
  typedef std::function<void(uint32_t priority, const HostVector& hosts_added,
                             const HostVector& hosts_removed)>
      MemberUpdateCb;
  Common::CallbackHandle* addMemberUpdateCb(MemberUpdateCb callback) const {
    return member_update_cb_helper_.add(callback);
  }

  // Upstream::HostSet
  const HostVector& hosts() const override { return *hosts_; }
  const HostVector& healthyHosts() const override { return *healthy_hosts_; }
  const HostsPerLocality& hostsPerLocality() const override { return *hosts_per_locality_; }
  const HostsPerLocality& healthyHostsPerLocality() const override {
    return *healthy_hosts_per_locality_;
  }
  LocalityWeightsConstSharedPtr localityWeights() const override { return locality_weights_; }
  absl::optional<uint32_t> chooseLocality() override;
  uint32_t priority() const override { return priority_; }
  uint32_t overprovisioning_factor() const override { return overprovisioning_factor_; }

protected:
  virtual void runUpdateCallbacks(const HostVector& hosts_added, const HostVector& hosts_removed) {
    member_update_cb_helper_.runCallbacks(priority_, hosts_added, hosts_removed);
  }

private:
  // Weight for a locality taking into account health status.
  double effectiveLocalityWeight(uint32_t index) const;

  uint32_t priority_;
  uint32_t overprovisioning_factor_;
  HostVectorConstSharedPtr hosts_;
  HostVectorConstSharedPtr healthy_hosts_;
  HostsPerLocalityConstSharedPtr hosts_per_locality_{HostsPerLocalityImpl::empty()};
  HostsPerLocalityConstSharedPtr healthy_hosts_per_locality_{HostsPerLocalityImpl::empty()};
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
  std::vector<std::shared_ptr<LocalityEntry>> locality_entries_;
  std::unique_ptr<EdfScheduler<LocalityEntry>> locality_scheduler_;
};

typedef std::unique_ptr<HostSetImpl> HostSetImplPtr;

/**
 * A class for management of the set of hosts in a given cluster.
 */

class PrioritySetImpl : public PrioritySet {
public:
  // From PrioritySet
  Common::CallbackHandle* addMemberUpdateCb(MemberUpdateCb callback) const override {
    return member_update_cb_helper_.add(callback);
  }
  const std::vector<std::unique_ptr<HostSet>>& hostSetsPerPriority() const override {
    return host_sets_;
  }
  std::vector<std::unique_ptr<HostSet>>& hostSetsPerPriority() override { return host_sets_; }
  // Get the host set for this priority level, creating it if necessary.
  HostSet& getOrCreateHostSet(uint32_t priority,
                              absl::optional<uint32_t> overprovisioning_factor = absl::nullopt);

protected:
  // Allows subclasses of PrioritySetImpl to create their own type of HostSetImpl.
  virtual HostSetImplPtr createHostSet(uint32_t priority,
                                       absl::optional<uint32_t> overprovisioning_factor) {
    return HostSetImplPtr{new HostSetImpl(priority, overprovisioning_factor)};
  }

private:
  virtual void runUpdateCallbacks(uint32_t priority, const HostVector& hosts_added,
                                  const HostVector& hosts_removed) {
    member_update_cb_helper_.runCallbacks(priority, hosts_added, hosts_removed);
  }
  // This vector will generally have at least one member, for priority level 0.
  // It will expand as host sets are added but currently does not shrink to
  // avoid any potential lifetime issues.
  std::vector<std::unique_ptr<HostSet>> host_sets_;
  // TODO(mattklein123): Remove mutable.
  mutable Common::CallbackManager<uint32_t, const HostVector&, const HostVector&>
      member_update_cb_helper_;
};

/**
 * Implementation of ClusterInfo that reads from JSON.
 */
class ClusterInfoImpl : public ClusterInfo {
public:
  ClusterInfoImpl(const envoy::api::v2::Cluster& config,
                  const envoy::api::v2::core::BindConfig& bind_config, Runtime::Loader& runtime,
                  Network::TransportSocketFactoryPtr&& socket_factory,
                  Stats::ScopePtr&& stats_scope, bool added_via_api);

  static ClusterStats generateStats(Stats::Scope& scope);
  static ClusterLoadReportStats generateLoadReportStats(Stats::Scope& scope);

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
  const Http::Http2Settings& http2Settings() const override { return http2_settings_; }
  ProtocolOptionsConfigConstSharedPtr
  extensionProtocolOptions(const std::string& name) const override;
  LoadBalancerType lbType() const override { return lb_type_; }
  envoy::api::v2::Cluster::DiscoveryType type() const override { return type_; }
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
  const std::string& name() const override { return name_; }
  ResourceManager& resourceManager(ResourcePriority priority) const override;
  Network::TransportSocketFactory& transportSocketFactory() const override {
    return *transport_socket_factory_;
  }
  ClusterStats& stats() const override { return stats_; }
  Stats::Scope& statsScope() const override { return *stats_scope_; }
  ClusterLoadReportStats& loadReportStats() const override { return load_report_stats_; }
  const Network::Address::InstanceConstSharedPtr& sourceAddress() const override {
    return source_address_;
  };
  const LoadBalancerSubsetInfo& lbSubsetInfo() const override { return lb_subset_; }
  const envoy::api::v2::core::Metadata& metadata() const override { return metadata_; }

  const Network::ConnectionSocket::OptionsSharedPtr& clusterSocketOptions() const override {
    return cluster_socket_options_;
  };

  bool drainConnectionsOnHostRemoval() const override { return drain_connections_on_host_removal_; }

private:
  struct ResourceManagers {
    ResourceManagers(const envoy::api::v2::Cluster& config, Runtime::Loader& runtime,
                     const std::string& cluster_name);
    ResourceManagerImplPtr load(const envoy::api::v2::Cluster& config, Runtime::Loader& runtime,
                                const std::string& cluster_name,
                                const envoy::api::v2::core::RoutingPriority& priority);

    typedef std::array<ResourceManagerImplPtr, NumResourcePriorities> Managers;

    Managers managers_;
  };

  Runtime::Loader& runtime_;
  const std::string name_;
  const envoy::api::v2::Cluster::DiscoveryType type_;
  const uint64_t max_requests_per_connection_;
  const std::chrono::milliseconds connect_timeout_;
  absl::optional<std::chrono::milliseconds> idle_timeout_;
  const uint32_t per_connection_buffer_limit_bytes_;
  Network::TransportSocketFactoryPtr transport_socket_factory_;
  Stats::ScopePtr stats_scope_;
  mutable ClusterStats stats_;
  Stats::IsolatedStoreImpl load_report_stats_store_;
  mutable ClusterLoadReportStats load_report_stats_;
  const uint64_t features_;
  const Http::Http2Settings http2_settings_;
  const std::map<std::string, ProtocolOptionsConfigConstSharedPtr> extension_protocol_options_;
  mutable ResourceManagers resource_managers_;
  const std::string maintenance_mode_runtime_key_;
  const Network::Address::InstanceConstSharedPtr source_address_;
  LoadBalancerType lb_type_;
  absl::optional<envoy::api::v2::Cluster::RingHashLbConfig> lb_ring_hash_config_;
  absl::optional<envoy::api::v2::Cluster::OriginalDstLbConfig> lb_original_dst_config_;
  const bool added_via_api_;
  LoadBalancerSubsetInfoImpl lb_subset_;
  const envoy::api::v2::core::Metadata metadata_;
  const envoy::api::v2::Cluster::CommonLbConfig common_lb_config_;
  const Network::ConnectionSocket::OptionsSharedPtr cluster_socket_options_;
  const bool drain_connections_on_host_removal_;
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
  static ClusterSharedPtr
  create(const envoy::api::v2::Cluster& cluster, ClusterManager& cm, Stats::Store& stats,
         ThreadLocal::Instance& tls, Network::DnsResolverSharedPtr dns_resolver,
         Ssl::ContextManager& ssl_context_manager, Runtime::Loader& runtime,
         Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
         AccessLog::AccessLogManager& log_manager, const LocalInfo::LocalInfo& local_info,
         Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api);
  // From Upstream::Cluster
  virtual PrioritySet& prioritySet() override { return priority_set_; }
  virtual const PrioritySet& prioritySet() const override { return priority_set_; }

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

  static HostVectorConstSharedPtr createHealthyHostList(const HostVector& hosts);
  static HostsPerLocalityConstSharedPtr createHealthyHostLists(const HostsPerLocality& hosts);

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

  Runtime::Loader& runtime_;
  Server::InitManagerImpl init_manager_;
  ClusterInfoConstSharedPtr info_; // This cluster info stores the stats scope so it must be
                                   // initialized first and destroyed last.
  HealthCheckerSharedPtr health_checker_;
  Outlier::DetectorSharedPtr outlier_detector_;

protected:
  PrioritySetImpl priority_set_;

private:
  void finishInitialization();
  void reloadHealthyHosts();

  bool initialization_started_{};
  std::function<void()> initialization_complete_callback_;
  uint64_t pending_initialize_health_checks_{};
};

/**
 * Manages PriorityState of a cluster. PriorityState is a per-priority binding of a set of hosts
 * with its corresponding locality weight map. This is useful to store priorities/hosts/localities
 * before updating the cluster priority set.
 */
class PriorityStateManager : protected Logger::Loggable<Logger::Id::upstream> {
public:
  PriorityStateManager(ClusterImplBase& cluster, const LocalInfo::LocalInfo& local_info);

  // Initializes the PriorityState vector based on the priority specified in locality_lb_endpoint.
  void
  initializePriorityFor(const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint);

  // Registers a host based on its address to the PriorityState based on the specified priority (the
  // priority is specified by locality_lb_endpoint.priority()).
  //
  // The specified health_checker_flag is used to set the registered-host's health-flag when the
  // lb_endpoint health status is unhealty, draining or timeout.
  void
  registerHostForPriority(const std::string& hostname,
                          Network::Address::InstanceConstSharedPtr address,
                          const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint,
                          const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint,
                          const absl::optional<Upstream::Host::HealthFlag> health_checker_flag);

  void
  registerHostForPriority(const HostSharedPtr& host,
                          const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint,
                          const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint,
                          const absl::optional<Upstream::Host::HealthFlag> health_checker_flag);

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
};

typedef std::unique_ptr<PriorityStateManager> PriorityStateManagerPtr;

/**
 * Implementation of Upstream::Cluster for static clusters (clusters that have a fixed number of
 * hosts with resolved IP addresses).
 */
class StaticClusterImpl : public ClusterImplBase {
public:
  StaticClusterImpl(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                    Server::Configuration::TransportSocketFactoryContext& factory_context,
                    Stats::ScopePtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  // ClusterImplBase
  void startPreInit() override;

  PriorityStateManagerPtr priority_state_manager_;
};

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
   * @param updated_hosts is used to aggregate the new state of all hosts accross priority, and will
   * be updated with the hosts that remain in this priority after the update.
   * @return whether the hosts for the priority changed.
   */
  bool updateDynamicHostList(const HostVector& new_hosts, HostVector& current_priority_hosts,
                             HostVector& hosts_added_to_current_priority,
                             HostVector& hosts_removed_from_current_priority,
                             std::unordered_map<std::string, HostSharedPtr>& updated_hosts);

  typedef std::unordered_map<std::string, Upstream::HostSharedPtr> HostMap;

  /**
   * Updates the internal collection of all hosts. This should be called with the updated
   * map of hosts after issuing updateDynamicHostList for each priority.
   *
   * @param all_hosts the updated map of address to host after a cluster update.
   */
  void updateHostMap(HostMap&& all_hosts) { all_hosts_ = std::move(all_hosts); }

private:
  HostMap all_hosts_;
};

/**
 * Implementation of Upstream::Cluster that does periodic DNS resolution and updates the host
 * member set if the DNS members change.
 */
class StrictDnsClusterImpl : public BaseDynamicClusterImpl {
public:
  StrictDnsClusterImpl(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                       Network::DnsResolverSharedPtr dns_resolver,
                       Server::Configuration::TransportSocketFactoryContext& factory_context,
                       Stats::ScopePtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  struct ResolveTarget {
    ResolveTarget(StrictDnsClusterImpl& parent, Event::Dispatcher& dispatcher,
                  const std::string& url,
                  const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint,
                  const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint);
    ~ResolveTarget();
    void startResolve();

    StrictDnsClusterImpl& parent_;
    Network::ActiveDnsQuery* active_query_{};
    std::string dns_address_;
    uint32_t port_;
    Event::TimerPtr resolve_timer_;
    HostVector hosts_;
    const envoy::api::v2::endpoint::LocalityLbEndpoints locality_lb_endpoint_;
    const envoy::api::v2::endpoint::LbEndpoint lb_endpoint_;
  };

  typedef std::unique_ptr<ResolveTarget> ResolveTargetPtr;

  void updateAllHosts(const HostVector& hosts_added, const HostVector& hosts_removed,
                      uint32_t priority);

  // ClusterImplBase
  void startPreInit() override;

  const LocalInfo::LocalInfo& local_info_;
  Network::DnsResolverSharedPtr dns_resolver_;
  std::list<ResolveTargetPtr> resolve_targets_;
  const std::chrono::milliseconds dns_refresh_rate_ms_;
  Network::DnsLookupFamily dns_lookup_family_;
};

} // namespace Upstream
} // namespace Envoy
