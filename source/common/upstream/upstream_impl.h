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

#include "envoy/common/callback.h"
#include "envoy/common/time.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
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

#include "source/common/common/callback_impl.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/config/metadata.h"
#include "source/common/config/well_known_names.h"
#include "source/common/http/filter_chain_helper.h"
#include "source/common/http/http1/codec_stats.h"
#include "source/common/http/http2/codec_stats.h"
#include "source/common/http/http3/codec_stats.h"
#include "source/common/init/manager_impl.h"
#include "source/common/network/utility.h"
#include "source/common/shared_pool/shared_pool.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/upstream/load_balancer_impl.h"
#include "source/common/upstream/outlier_detection_impl.h"
#include "source/common/upstream/resource_manager_impl.h"
#include "source/common/upstream/transport_socket_match_impl.h"
#include "source/common/upstream/upstream_http_factory_context_impl.h"
#include "source/extensions/upstreams/http/config.h"
#include "source/server/transport_socket_config_impl.h"

#include "absl/container/node_hash_set.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Upstream {

/**
 * An implementation of UpstreamLocalAddressSelector.
 */
class UpstreamLocalAddressSelectorImpl : public UpstreamLocalAddressSelector {
public:
  UpstreamLocalAddressSelectorImpl(
      const envoy::config::cluster::v3::Cluster& config,
      const envoy::config::core::v3::BindConfig& bootstrap_bind_config);

  // UpstreamLocalAddressSelector
  UpstreamLocalAddress getUpstreamLocalAddress(
      const Network::Address::InstanceConstSharedPtr& endpoint_address,
      const Network::ConnectionSocket::OptionsSharedPtr& socket_options) const override;

private:
  const Network::ConnectionSocket::OptionsSharedPtr
  buildBaseSocketOptions(const envoy::config::cluster::v3::Cluster& config,
                         const envoy::config::core::v3::BindConfig& bootstrap_bind_config);
  const Network::ConnectionSocket::OptionsSharedPtr
  buildClusterSocketOptions(const envoy::config::cluster::v3::Cluster& config,
                            const envoy::config::core::v3::BindConfig bind_config);
  void parseBindConfig(const std::string cluster_name,
                       const envoy::config::core::v3::BindConfig& bind_config,
                       const Network::ConnectionSocket::OptionsSharedPtr& base_socket_options,
                       const Network::ConnectionSocket::OptionsSharedPtr& cluster_socket_options);
  Network::ConnectionSocket::OptionsSharedPtr combineConnectionSocketOptions(
      const Network::ConnectionSocket::OptionsSharedPtr& local_address_options,
      const Network::ConnectionSocket::OptionsSharedPtr& options) const;

  Network::ConnectionSocket::OptionsSharedPtr base_socket_options_;
  Network::ConnectionSocket::OptionsSharedPtr cluster_socket_options_;
  std::vector<UpstreamLocalAddress> upstream_local_addresses_;
};

/**
 * Null implementation of HealthCheckHostMonitor.
 */
class HealthCheckHostMonitorNullImpl : public HealthCheckHostMonitor {
public:
  // Upstream::HealthCheckHostMonitor
  void setUnhealthy(UnhealthyType) override {}
};

/**
 * Implementation of LoadMetricStats.
 */
class LoadMetricStatsImpl : public LoadMetricStats {
public:
  void add(const absl::string_view key, double value) override;
  StatMapPtr latch() override;

private:
  absl::Mutex mu_;
  StatMapPtr map_ ABSL_GUARDED_BY(mu_);
};

/**
 * Implementation of Upstream::HostDescription.
 */
class HostDescriptionImpl : virtual public HostDescription,
                            protected Logger::Loggable<Logger::Id::upstream> {
public:
  HostDescriptionImpl(
      ClusterInfoConstSharedPtr cluster, const std::string& hostname,
      Network::Address::InstanceConstSharedPtr dest_address, MetadataConstSharedPtr metadata,
      const envoy::config::core::v3::Locality& locality,
      const envoy::config::endpoint::v3::Endpoint::HealthCheckConfig& health_check_config,
      uint32_t priority, TimeSource& time_source);

  Network::UpstreamTransportSocketFactory& transportSocketFactory() const override {
    absl::ReaderMutexLock lock(&metadata_mutex_);
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
  MetadataConstSharedPtr metadata() const override {
    absl::ReaderMutexLock lock(&metadata_mutex_);
    return metadata_;
  }
  void metadata(MetadataConstSharedPtr new_metadata) override {
    auto& new_socket_factory = resolveTransportSocketFactory(address_, new_metadata.get());
    {
      absl::WriterMutexLock lock(&metadata_mutex_);
      metadata_ = new_metadata;
      // Update data members dependent on metadata.
      socket_factory_ = new_socket_factory;
    }
  }

  const ClusterInfo& cluster() const override { return *cluster_; }
  HealthCheckHostMonitor& healthChecker() const override {
    if (health_checker_) {
      return *health_checker_;
    }

    static HealthCheckHostMonitorNullImpl* null_health_checker =
        new HealthCheckHostMonitorNullImpl();
    return *null_health_checker;
  }

  bool canCreateConnection(Upstream::ResourcePriority priority) const override {
    if (stats().cx_active_.value() >= cluster().resourceManager(priority).maxConnectionsPerHost()) {
      return false;
    }
    return cluster().resourceManager(priority).connections().canCreate();
  }

  Outlier::DetectorHostMonitor& outlierDetector() const override {
    if (outlier_detector_) {
      return *outlier_detector_;
    }

    static Outlier::DetectorHostMonitorNullImpl* null_outlier_detector =
        new Outlier::DetectorHostMonitorNullImpl();
    return *null_outlier_detector;
  }
  HostStats& stats() const override { return stats_; }
  LoadMetricStats& loadMetricStats() const override { return load_metric_stats_; }
  const std::string& hostnameForHealthChecks() const override { return health_checks_hostname_; }
  const std::string& hostname() const override { return hostname_; }
  Network::Address::InstanceConstSharedPtr address() const override { return address_; }
  const std::vector<Network::Address::InstanceConstSharedPtr>& addressList() const override {
    return address_list_;
  }
  Network::Address::InstanceConstSharedPtr healthCheckAddress() const override {
    return health_check_address_;
  }
  const envoy::config::core::v3::Locality& locality() const override { return locality_; }
  Stats::StatName localityZoneStatName() const override {
    return locality_zone_stat_name_.statName();
  }
  uint32_t priority() const override { return priority_; }
  void priority(uint32_t priority) override { priority_ = priority; }
  Network::UpstreamTransportSocketFactory&
  resolveTransportSocketFactory(const Network::Address::InstanceConstSharedPtr& dest_address,
                                const envoy::config::core::v3::Metadata* metadata) const;
  MonotonicTime creationTime() const override { return creation_time_; }

  void setAddressList(const std::vector<Network::Address::InstanceConstSharedPtr>& address_list) {
    address_list_ = address_list;
  }

protected:
  void setAddress(Network::Address::InstanceConstSharedPtr address) { address_ = address; }

  void setHealthCheckAddress(Network::Address::InstanceConstSharedPtr address) {
    health_check_address_ = address;
  }

  void setHealthCheckerImpl(HealthCheckHostMonitorPtr&& health_checker) {
    health_checker_ = std::move(health_checker);
  }

  void setOutlierDetectorImpl(Outlier::DetectorHostMonitorPtr&& outlier_detector) {
    outlier_detector_ = std::move(outlier_detector);
  }

private:
  ClusterInfoConstSharedPtr cluster_;
  const std::string hostname_;
  const std::string health_checks_hostname_;
  Network::Address::InstanceConstSharedPtr address_;
  std::vector<Network::Address::InstanceConstSharedPtr> address_list_;
  Network::Address::InstanceConstSharedPtr health_check_address_;
  std::atomic<bool> canary_;
  mutable absl::Mutex metadata_mutex_;
  MetadataConstSharedPtr metadata_ ABSL_GUARDED_BY(metadata_mutex_);
  const envoy::config::core::v3::Locality locality_;
  Stats::StatNameDynamicStorage locality_zone_stat_name_;
  mutable HostStats stats_;
  mutable LoadMetricStatsImpl load_metric_stats_;
  Outlier::DetectorHostMonitorPtr outlier_detector_;
  HealthCheckHostMonitorPtr health_checker_;
  std::atomic<uint32_t> priority_;
  std::reference_wrapper<Network::UpstreamTransportSocketFactory>
      socket_factory_ ABSL_GUARDED_BY(metadata_mutex_);
  const MonotonicTime creation_time_;
};

/**
 * Implementation of Upstream::Host.
 */
class HostImpl : public HostDescriptionImpl,
                 public Host,
                 public std::enable_shared_from_this<HostImpl> {
public:
  HostImpl(ClusterInfoConstSharedPtr cluster, const std::string& hostname,
           Network::Address::InstanceConstSharedPtr address, MetadataConstSharedPtr metadata,
           uint32_t initial_weight, const envoy::config::core::v3::Locality& locality,
           const envoy::config::endpoint::v3::Endpoint::HealthCheckConfig& health_check_config,
           uint32_t priority, const envoy::config::core::v3::HealthStatus health_status,
           TimeSource& time_source)
      : HostDescriptionImpl(cluster, hostname, address, metadata, locality, health_check_config,
                            priority, time_source),
        disable_active_health_check_(health_check_config.disable_active_health_check()),
        health_status_(health_status) {
    // This EDS flags setting is still necessary for stats, configuration dump, canonical
    // coarseHealth() etc.
    setEdsHealthFlag(health_status);
    HostImpl::weight(initial_weight);
  }

  bool disableActiveHealthCheck() const override { return disable_active_health_check_; }
  void setDisableActiveHealthCheck(bool disable_active_health_check) override {
    disable_active_health_check_ = disable_active_health_check;
  }

  // Upstream::Host
  std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>>
  counters() const override {
    return stats().counters();
  }
  CreateConnectionData createConnection(
      Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
      Network::TransportSocketOptionsConstSharedPtr transport_socket_options) const override;
  CreateConnectionData createHealthCheckConnection(
      Event::Dispatcher& dispatcher,
      Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
      const envoy::config::core::v3::Metadata* metadata) const override;

  std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>>
  gauges() const override {
    return stats().gauges();
  }
  void healthFlagClear(HealthFlag flag) override { health_flags_ &= ~enumToInt(flag); }
  bool healthFlagGet(HealthFlag flag) const override { return health_flags_ & enumToInt(flag); }
  void healthFlagSet(HealthFlag flag) final { health_flags_ |= enumToInt(flag); }

  void setHealthChecker(HealthCheckHostMonitorPtr&& health_checker) override {
    setHealthCheckerImpl(std::move(health_checker));
  }
  void setOutlierDetector(Outlier::DetectorHostMonitorPtr&& outlier_detector) override {
    setOutlierDetectorImpl(std::move(outlier_detector));
  }

  Host::HealthStatus healthStatus() const override {
    // Evaluate active health status first.

    // Active unhealthy.
    if (healthFlagGet(HealthFlag::FAILED_ACTIVE_HC) ||
        healthFlagGet(HealthFlag::FAILED_OUTLIER_CHECK)) {
      return HealthStatus::UNHEALTHY;
    }
    // Active degraded.
    if (healthFlagGet(HealthFlag::DEGRADED_ACTIVE_HC)) {
      return HealthStatus::DEGRADED;
    }

    // Use EDS status if no any active status.
    return health_status_;
  }

  Host::Health coarseHealth() const override {
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
  bool used() const override { return handle_count_ > 0; }
  HostHandlePtr acquireHandle() const override {
    return std::make_unique<HostHandleImpl>(shared_from_this());
  }

protected:
  static CreateConnectionData
  createConnection(Event::Dispatcher& dispatcher, const ClusterInfo& cluster,
                   const Network::Address::InstanceConstSharedPtr& address,
                   const std::vector<Network::Address::InstanceConstSharedPtr>& address_list,
                   Network::UpstreamTransportSocketFactory& socket_factory,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
                   HostDescriptionConstSharedPtr host);

private:
  void setEdsHealthFlag(envoy::config::core::v3::HealthStatus health_status);

  std::atomic<uint32_t> health_flags_{};
  std::atomic<uint32_t> weight_;
  bool disable_active_health_check_;
  // TODO(wbpcode): should we store the EDS health status to health_flags_ to get unified status or
  // flag access? May be we could refactor HealthFlag to contain all these statuses and flags in the
  // future.
  HealthStatus health_status_{};

  struct HostHandleImpl : HostHandle {
    HostHandleImpl(const std::shared_ptr<const HostImpl>& parent) : parent_(parent) {
      parent->handle_count_++;
    }
    ~HostHandleImpl() override {
      if (const auto host = parent_.lock()) {
        ASSERT(host->handle_count_ > 0);
        host->handle_count_--;
      }
    }
    const std::weak_ptr<const HostImpl> parent_;
  };
  mutable std::atomic<uint32_t> handle_count_{};
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
   * @return Common::CallbackHandlePtr the callback handle.
   */
  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addPriorityUpdateCb(PrioritySet::PriorityUpdateCb callback) const {
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
  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addMemberUpdateCb(MemberUpdateCb callback) const override {
    return member_update_cb_helper_.add(callback);
  }
  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addPriorityUpdateCb(PriorityUpdateCb callback) const override {
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
                   absl::optional<uint32_t> overprovisioning_factor = absl::nullopt,
                   HostMapConstSharedPtr cross_priority_host_map = nullptr) override;

  void batchHostUpdate(BatchUpdateCb& callback) override;

  HostMapConstSharedPtr crossPriorityHostMap() const override {
    return const_cross_priority_host_map_;
  }

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

  // Read only all host map for fast host searching. This will never be null.
  mutable HostMapConstSharedPtr const_cross_priority_host_map_{std::make_shared<HostMap>()};

private:
  // This is a matching vector to store the callback handles for host_sets_. It is kept separately
  // because host_sets_ is directly returned so we avoid translation.
  std::vector<Common::CallbackHandlePtr> host_sets_priority_update_cbs_;
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

    absl::node_hash_set<HostSharedPtr> all_hosts_added_;
    absl::node_hash_set<HostSharedPtr> all_hosts_removed_;

  private:
    PrioritySetImpl& parent_;
    absl::node_hash_set<uint32_t> priorities_;
  };
};

/**
 * Specialized PrioritySetImpl designed for the main thread. It will update and maintain the read
 * only cross priority host map when the host set changes.
 */
class MainPrioritySetImpl : public PrioritySetImpl, public Logger::Loggable<Logger::Id::upstream> {
public:
  // PrioritySet
  void updateHosts(uint32_t priority, UpdateHostsParams&& update_hosts_params,
                   LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
                   const HostVector& hosts_removed,
                   absl::optional<uint32_t> overprovisioning_factor = absl::nullopt,
                   HostMapConstSharedPtr cross_priority_host_map = nullptr) override;
  HostMapConstSharedPtr crossPriorityHostMap() const override;

protected:
  void updateCrossPriorityHostMap(const HostVector& hosts_added, const HostVector& hosts_removed);

  mutable HostMapSharedPtr mutable_cross_priority_host_map_;
};

/**
 * Implementation of ClusterInfo that reads from JSON.
 */
class ClusterInfoImpl : public ClusterInfo,
                        public Event::DispatcherThreadDeletable,
                        protected Logger::Loggable<Logger::Id::upstream> {
public:
  using HttpProtocolOptionsConfigImpl =
      Envoy::Extensions::Upstreams::Http::ProtocolOptionsConfigImpl;
  ClusterInfoImpl(Init::Manager& info, Server::Configuration::ServerFactoryContext& server_context,
                  const envoy::config::cluster::v3::Cluster& config,
                  const envoy::config::core::v3::BindConfig& bind_config, Runtime::Loader& runtime,
                  TransportSocketMatcherPtr&& socket_matcher, Stats::ScopeSharedPtr&& stats_scope,
                  bool added_via_api, Server::Configuration::TransportSocketFactoryContext&);

  static ClusterTrafficStats generateStats(Stats::Scope& scope,
                                           const ClusterTrafficStatNames& cluster_stat_names);
  static ClusterLoadReportStats
  generateLoadReportStats(Stats::Scope& scope, const ClusterLoadReportStatNames& stat_names);
  static ClusterCircuitBreakersStats
  generateCircuitBreakersStats(Stats::Scope& scope, Stats::StatName prefix, bool track_remaining,
                               const ClusterCircuitBreakersStatNames& stat_names);
  static ClusterRequestResponseSizeStats
  generateRequestResponseSizeStats(Stats::Scope&,
                                   const ClusterRequestResponseSizeStatNames& stat_names);
  static ClusterTimeoutBudgetStats
  generateTimeoutBudgetStats(Stats::Scope&, const ClusterTimeoutBudgetStatNames& stat_names);

  // Upstream::ClusterInfo
  bool addedViaApi() const override { return added_via_api_; }
  const ProtobufTypes::MessagePtr& loadBalancingPolicy() const override {
    return load_balancing_policy_;
  }
  TypedLoadBalancerFactory* loadBalancerFactory() const override { return load_balancer_factory_; }
  const envoy::config::cluster::v3::Cluster::CommonLbConfig& lbConfig() const override {
    return common_lb_config_;
  }
  std::chrono::milliseconds connectTimeout() const override { return connect_timeout_; }
  const absl::optional<std::chrono::milliseconds> idleTimeout() const override {
    return idle_timeout_;
  }
  const absl::optional<std::chrono::milliseconds> maxConnectionDuration() const override {
    return max_connection_duration_;
  }
  float perUpstreamPreconnectRatio() const override { return per_upstream_preconnect_ratio_; }
  float peekaheadRatio() const override { return peekahead_ratio_; }
  uint32_t perConnectionBufferLimitBytes() const override {
    return per_connection_buffer_limit_bytes_;
  }
  uint64_t features() const override { return features_; }
  const Http::Http1Settings& http1Settings() const override {
    return http_protocol_options_->http1_settings_;
  }
  const envoy::config::core::v3::Http2ProtocolOptions& http2Options() const override {
    return http_protocol_options_->http2_options_;
  }
  const envoy::config::core::v3::Http3ProtocolOptions& http3Options() const override {
    return http_protocol_options_->http3_options_;
  }
  const envoy::config::core::v3::HttpProtocolOptions& commonHttpProtocolOptions() const override {
    return http_protocol_options_->common_http_protocol_options_;
  }
  void configureLbPolicies(const envoy::config::cluster::v3::Cluster& config,
                           Server::Configuration::ServerFactoryContext& context);
  ProtocolOptionsConfigConstSharedPtr
  extensionProtocolOptions(const std::string& name) const override;
  LoadBalancerType lbType() const override { return lb_type_; }
  envoy::config::cluster::v3::Cluster::DiscoveryType type() const override { return type_; }
  const absl::optional<envoy::config::cluster::v3::Cluster::CustomClusterType>&
  clusterType() const override {
    return cluster_type_;
  }
  const absl::optional<envoy::config::cluster::v3::Cluster::RoundRobinLbConfig>&
  lbRoundRobinConfig() const override {
    return lb_round_robin_config_;
  }
  const absl::optional<envoy::config::cluster::v3::Cluster::LeastRequestLbConfig>&
  lbLeastRequestConfig() const override {
    return lb_least_request_config_;
  }
  const absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig>&
  lbRingHashConfig() const override {
    return lb_ring_hash_config_;
  }
  const absl::optional<envoy::config::cluster::v3::Cluster::MaglevLbConfig>&
  lbMaglevConfig() const override {
    return lb_maglev_config_;
  }
  const absl::optional<envoy::config::cluster::v3::Cluster::OriginalDstLbConfig>&
  lbOriginalDstConfig() const override {
    return lb_original_dst_config_;
  }
  const absl::optional<envoy::config::core::v3::TypedExtensionConfig>&
  upstreamConfig() const override {
    return upstream_config_;
  }
  bool maintenanceMode() const override;
  uint64_t maxRequestsPerConnection() const override { return max_requests_per_connection_; }
  uint32_t maxResponseHeadersCount() const override { return max_response_headers_count_; }
  const std::string& name() const override { return name_; }
  const std::string& observabilityName() const override { return observability_name_; }
  ResourceManager& resourceManager(ResourcePriority priority) const override;
  TransportSocketMatcher& transportSocketMatcher() const override { return *socket_matcher_; }
  ClusterTrafficStats& trafficStats() const override { return stats_; }
  ClusterConfigUpdateStats& configUpdateStats() const override { return config_update_stats_; }
  ClusterLbStats& lbStats() const override { return lb_stats_; }
  ClusterEndpointStats& endpointStats() const override { return endpoint_stats_; }
  Stats::Scope& statsScope() const override { return *stats_scope_; }

  ClusterRequestResponseSizeStatsOptRef requestResponseSizeStats() const override {
    if (optional_cluster_stats_ == nullptr ||
        optional_cluster_stats_->request_response_size_stats_ == nullptr) {
      return absl::nullopt;
    }

    return std::ref(*(optional_cluster_stats_->request_response_size_stats_));
  }

  ClusterLoadReportStats& loadReportStats() const override { return load_report_stats_; }

  ClusterTimeoutBudgetStatsOptRef timeoutBudgetStats() const override {
    if (optional_cluster_stats_ == nullptr ||
        optional_cluster_stats_->timeout_budget_stats_ == nullptr) {
      return absl::nullopt;
    }

    return std::ref(*(optional_cluster_stats_->timeout_budget_stats_));
  }

  std::shared_ptr<UpstreamLocalAddressSelector> getUpstreamLocalAddressSelector() const override {
    return upstream_local_address_selector_;
  }
  const LoadBalancerSubsetInfo& lbSubsetInfo() const override { return lb_subset_; }
  const envoy::config::core::v3::Metadata& metadata() const override { return metadata_; }
  const Envoy::Config::TypedMetadata& typedMetadata() const override { return typed_metadata_; }

  bool drainConnectionsOnHostRemoval() const override { return drain_connections_on_host_removal_; }
  bool connectionPoolPerDownstreamConnection() const override {
    return connection_pool_per_downstream_connection_;
  }
  bool warmHosts() const override { return warm_hosts_; }
  bool setLocalInterfaceNameOnUpstreamConnections() const override {
    return set_local_interface_name_on_upstream_connections_;
  }
  const absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>&
  upstreamHttpProtocolOptions() const override {
    return http_protocol_options_->upstream_http_protocol_options_;
  }

  const absl::optional<envoy::config::core::v3::AlternateProtocolsCacheOptions>&
  alternateProtocolsCacheOptions() const override {
    return http_protocol_options_->alternate_protocol_cache_options_;
  }

  absl::optional<std::string> edsServiceName() const override { return eds_service_name_; }

  void createNetworkFilterChain(Network::Connection&) const override;
  std::vector<Http::Protocol>
  upstreamHttpProtocol(absl::optional<Http::Protocol> downstream_protocol) const override;

  // Http::FilterChainFactory
  bool createFilterChain(Http::FilterChainManager& manager,
                         bool only_create_if_configured) const override {
    if (!has_configured_http_filters_ && only_create_if_configured) {
      return false;
    }
    Http::FilterChainUtility::createFilterChainForFactories(manager, http_filter_factories_);
    return true;
  }
  bool createUpgradeFilterChain(absl::string_view, const UpgradeMap*,
                                Http::FilterChainManager&) const override {
    // Upgrade filter chains not yet supported for upstream filters.
    return false;
  }

  Http::Http1::CodecStats& http1CodecStats() const override;
  Http::Http2::CodecStats& http2CodecStats() const override;
  Http::Http3::CodecStats& http3CodecStats() const override;

protected:
  // Gets the retry budget percent/concurrency from the circuit breaker thresholds. If the retry
  // budget message is specified, defaults will be filled in if either params are unspecified.
  static std::pair<absl::optional<double>, absl::optional<uint32_t>>
  getRetryBudgetParams(const envoy::config::cluster::v3::CircuitBreakers::Thresholds& thresholds);

private:
  struct ResourceManagers {
    ResourceManagers(const envoy::config::cluster::v3::Cluster& config, Runtime::Loader& runtime,
                     const std::string& cluster_name, Stats::Scope& stats_scope,
                     const ClusterCircuitBreakersStatNames& circuit_breakers_stat_names);
    ResourceManagerImplPtr load(const envoy::config::cluster::v3::Cluster& config,
                                Runtime::Loader& runtime, const std::string& cluster_name,
                                Stats::Scope& stats_scope,
                                const envoy::config::core::v3::RoutingPriority& priority);

    using Managers = std::array<ResourceManagerImplPtr, NumResourcePriorities>;

    Managers managers_;
    const ClusterCircuitBreakersStatNames& circuit_breakers_stat_names_;
  };

  struct OptionalClusterStats {
    OptionalClusterStats(const envoy::config::cluster::v3::Cluster& config,
                         Stats::Scope& stats_scope, const ClusterManager& manager);
    const ClusterTimeoutBudgetStatsPtr timeout_budget_stats_;
    const ClusterRequestResponseSizeStatsPtr request_response_size_stats_;
  };

  Runtime::Loader& runtime_;
  const std::string name_;
  const std::string observability_name_;
  const envoy::config::cluster::v3::Cluster::DiscoveryType type_;
  const absl::flat_hash_map<std::string, ProtocolOptionsConfigConstSharedPtr>
      extension_protocol_options_;
  const std::shared_ptr<const HttpProtocolOptionsConfigImpl> http_protocol_options_;
  const uint64_t max_requests_per_connection_;
  const uint32_t max_response_headers_count_;
  const std::chrono::milliseconds connect_timeout_;
  absl::optional<std::chrono::milliseconds> idle_timeout_;
  absl::optional<std::chrono::milliseconds> max_connection_duration_;
  const float per_upstream_preconnect_ratio_;
  const float peekahead_ratio_;
  const uint32_t per_connection_buffer_limit_bytes_;
  TransportSocketMatcherPtr socket_matcher_;
  Stats::ScopeSharedPtr stats_scope_;
  mutable ClusterTrafficStats stats_;
  mutable ClusterConfigUpdateStats config_update_stats_;
  mutable ClusterLbStats lb_stats_;
  mutable ClusterEndpointStats endpoint_stats_;
  Stats::IsolatedStoreImpl load_report_stats_store_;
  mutable ClusterLoadReportStats load_report_stats_;
  const std::unique_ptr<OptionalClusterStats> optional_cluster_stats_;
  const uint64_t features_;
  mutable ResourceManagers resource_managers_;
  const std::string maintenance_mode_runtime_key_;
  std::shared_ptr<UpstreamLocalAddressSelector> upstream_local_address_selector_;
  LoadBalancerType lb_type_;
  absl::optional<envoy::config::cluster::v3::Cluster::RoundRobinLbConfig> lb_round_robin_config_;
  absl::optional<envoy::config::cluster::v3::Cluster::LeastRequestLbConfig>
      lb_least_request_config_;
  absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig> lb_ring_hash_config_;
  absl::optional<envoy::config::cluster::v3::Cluster::MaglevLbConfig> lb_maglev_config_;
  absl::optional<envoy::config::cluster::v3::Cluster::OriginalDstLbConfig> lb_original_dst_config_;
  absl::optional<envoy::config::core::v3::TypedExtensionConfig> upstream_config_;
  const bool added_via_api_;
  LoadBalancerSubsetInfoImpl lb_subset_;
  const envoy::config::core::v3::Metadata metadata_;
  Envoy::Config::TypedMetadataImpl<ClusterTypedMetadataFactory> typed_metadata_;
  ProtobufTypes::MessagePtr load_balancing_policy_;
  TypedLoadBalancerFactory* load_balancer_factory_ = nullptr;
  const envoy::config::cluster::v3::Cluster::CommonLbConfig common_lb_config_;
  const bool drain_connections_on_host_removal_;
  const bool connection_pool_per_downstream_connection_;
  const bool warm_hosts_;
  const bool set_local_interface_name_on_upstream_connections_;
  const absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>
      upstream_http_protocol_options_;
  absl::optional<std::string> eds_service_name_;
  const absl::optional<envoy::config::cluster::v3::Cluster::CustomClusterType> cluster_type_;
  const std::unique_ptr<Server::Configuration::CommonFactoryContext> factory_context_;
  std::vector<Network::FilterFactoryCb> filter_factories_;
  Http::FilterChainUtility::FilterFactoriesList http_filter_factories_;
  // true iff the cluster proto specified upstream http filters.
  bool has_configured_http_filters_{false};
  mutable Http::Http1::CodecStats::AtomicPtr http1_codec_stats_;
  mutable Http::Http2::CodecStats::AtomicPtr http2_codec_stats_;
  mutable Http::Http3::CodecStats::AtomicPtr http3_codec_stats_;
  UpstreamHttpFactoryContextImpl upstream_context_;
};

/**
 * Function that creates a Network::UpstreamTransportSocketFactoryPtr
 * given a cluster configuration and transport socket factory
 * context.
 */
Network::UpstreamTransportSocketFactoryPtr
createTransportSocketFactory(const envoy::config::cluster::v3::Cluster& config,
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
   * Set the TransportSocketFactoryContext pointer so that the memory isn't lost
   * for the cluster lifetime in case SDS is used.
   */
  void setTransportFactoryContext(
      Server::Configuration::TransportSocketFactoryContextPtr transport_factory_context);

  /**
   * Wrapper around Network::Address::resolveProtoAddress() that provides improved error message
   * based on the cluster's type.
   * @param address supplies the address proto to resolve.
   * @return Network::Address::InstanceConstSharedPtr the resolved address.
   */
  const Network::Address::InstanceConstSharedPtr
  resolveProtoAddress(const envoy::config::core::v3::Address& address);

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
  Config::ConstMetadataSharedPoolSharedPtr constMetadataSharedPool() {
    return const_metadata_shared_pool_;
  }

  // Upstream::Cluster
  HealthChecker* healthChecker() override { return health_checker_.get(); }
  ClusterInfoConstSharedPtr info() const override { return info_; }
  Outlier::Detector* outlierDetector() override { return outlier_detector_.get(); }
  const Outlier::Detector* outlierDetector() const override { return outlier_detector_.get(); }
  void initialize(std::function<void()> callback) override;

protected:
  ClusterImplBase(Server::Configuration::ServerFactoryContext& server_context,
                  const envoy::config::cluster::v3::Cluster& cluster, Runtime::Loader& runtime,
                  Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
                  Stats::ScopeSharedPtr&& stats_scope, bool added_via_api, TimeSource& time_source);

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
  const bool wait_for_warm_on_init_;

protected:
  TimeSource& time_source_;
  MainPrioritySetImpl priority_set_;

  void validateEndpointsForZoneAwareRouting(
      const envoy::config::endpoint::v3::LocalityLbEndpoints& endpoints) const;

private:
  void finishInitialization();
  void reloadHealthyHosts(const HostSharedPtr& host);

  bool initialization_started_{};
  std::function<void()> initialization_complete_callback_;
  uint64_t pending_initialize_health_checks_{};
  const bool local_cluster_;
  Config::ConstMetadataSharedPoolSharedPtr const_metadata_shared_pool_;
  Common::CallbackHandlePtr priority_update_cb_;
  Server::Configuration::TransportSocketFactoryContextPtr transport_factory_context_{};
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
  void initializePriorityFor(
      const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint);

  // Registers a host based on its address to the PriorityState based on the specified priority (the
  // priority is specified by locality_lb_endpoint.priority()).
  //
  // The specified health_checker_flag is used to set the registered-host's health-flag when the
  // lb_endpoint health status is unhealthy, draining or timeout.
  void registerHostForPriority(
      const std::string& hostname, Network::Address::InstanceConstSharedPtr address,
      const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint,
      const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint, TimeSource& time_source);

  void registerHostForPriority(
      const HostSharedPtr& host,
      const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint);

  void
  updateClusterPrioritySet(const uint32_t priority, HostVectorSharedPtr&& current_hosts,
                           const absl::optional<HostVector>& hosts_added,
                           const absl::optional<HostVector>& hosts_removed,
                           const absl::optional<Upstream::Host::HealthFlag> health_checker_flag,
                           absl::optional<uint32_t> overprovisioning_factor = absl::nullopt);

  // Returns the saved priority state.
  PriorityState& priorityState() { return priority_state_; }

private:
  ClusterImplBase& parent_;
  PriorityState priority_state_;
  const envoy::config::core::v3::Node& local_info_node_;
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
   * @param all_hosts all known hosts prior to this host update across all priorities.
   * @param all_new_hosts addresses of all hosts in the new configuration across all priorities.
   * @return whether the hosts for the priority changed.
   */
  bool updateDynamicHostList(const HostVector& new_hosts, HostVector& current_priority_hosts,
                             HostVector& hosts_added_to_current_priority,
                             HostVector& hosts_removed_from_current_priority,
                             const HostMap& all_hosts,
                             const absl::flat_hash_set<std::string>& all_new_hosts);
};

/**
 * Utility function to get Dns from cluster/enum.
 */
Network::DnsLookupFamily
getDnsLookupFamilyFromCluster(const envoy::config::cluster::v3::Cluster& cluster);

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

/**
 * Utility function to resolve health check address.
 */
Network::Address::InstanceConstSharedPtr resolveHealthCheckAddress(
    const envoy::config::endpoint::v3::Endpoint::HealthCheckConfig& health_check_config,
    Network::Address::InstanceConstSharedPtr host_address);

} // namespace Upstream
} // namespace Envoy
