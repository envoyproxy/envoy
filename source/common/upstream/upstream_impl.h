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

#include "envoy/api/v2/base.pb.h"
#include "envoy/event/timer.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "common/common/callback_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/stats/stats_impl.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/outlier_detection_impl.h"
#include "common/upstream/resource_manager_impl.h"

namespace Envoy {
namespace Upstream {

// Wrapper around envoy::api::v2::Locality to make it easier to compare for ordering in std::map and
// in tests to construct literals.
// TODO(htuch): Consider making this reference based when we have a single string implementation.
class Locality : public std::tuple<std::string, std::string, std::string> {
public:
  Locality(const std::string& region, const std::string& zone, const std::string& sub_zone)
      : std::tuple<std::string, std::string, std::string>(region, zone, sub_zone) {}
  Locality(const envoy::api::v2::Locality& locality)
      : std::tuple<std::string, std::string, std::string>(locality.region(), locality.zone(),
                                                          locality.sub_zone()) {}
  bool empty() const {
    return std::get<0>(*this).empty() && std::get<1>(*this).empty() && std::get<2>(*this).empty();
  }
};

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
  HostDescriptionImpl(ClusterInfoConstSharedPtr cluster, const std::string& hostname,
                      Network::Address::InstanceConstSharedPtr dest_address,
                      const envoy::api::v2::Metadata& metadata,
                      const envoy::api::v2::Locality& locality)
      : cluster_(cluster), hostname_(hostname), address_(dest_address),
        canary_(Config::Metadata::metadataValue(metadata, Config::MetadataFilters::get().ENVOY_LB,
                                                Config::MetadataEnvoyLbKeys::get().CANARY)
                    .bool_value()),
        metadata_(metadata), locality_(locality), stats_{ALL_HOST_STATS(POOL_COUNTER(stats_store_),
                                                                        POOL_GAUGE(stats_store_))} {
  }

  // Upstream::HostDescription
  bool canary() const override { return canary_; }
  const envoy::api::v2::Metadata& metadata() const override { return metadata_; }
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
  const envoy::api::v2::Locality& locality() const override { return locality_; }

protected:
  ClusterInfoConstSharedPtr cluster_;
  const std::string hostname_;
  Network::Address::InstanceConstSharedPtr address_;
  const bool canary_;
  const envoy::api::v2::Metadata metadata_;
  const envoy::api::v2::Locality locality_;
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
           const envoy::api::v2::Metadata& metadata, uint32_t initial_weight,
           const envoy::api::v2::Locality& locality)
      : HostDescriptionImpl(cluster, hostname, address, metadata, locality), used_(true) {
    weight(initial_weight);
  }

  // Upstream::Host
  std::list<Stats::CounterSharedPtr> counters() const override { return stats_store_.counters(); }
  CreateConnectionData
  createConnection(Event::Dispatcher& dispatcher,
                   const Network::ConnectionSocket::OptionsSharedPtr& options) const override;
  std::list<Stats::GaugeSharedPtr> gauges() const override { return stats_store_.gauges(); }
  void healthFlagClear(HealthFlag flag) override { health_flags_ &= ~enumToInt(flag); }
  bool healthFlagGet(HealthFlag flag) const override { return health_flags_ & enumToInt(flag); }
  void healthFlagSet(HealthFlag flag) override { health_flags_ |= enumToInt(flag); }
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
  std::atomic<uint32_t> weight_;
  std::atomic<bool> used_;
};

typedef std::shared_ptr<std::vector<HostSharedPtr>> HostVectorSharedPtr;
typedef std::shared_ptr<const std::vector<HostSharedPtr>> HostVectorConstSharedPtr;
typedef std::shared_ptr<std::vector<std::vector<HostSharedPtr>>> HostListsSharedPtr;
typedef std::shared_ptr<const std::vector<std::vector<HostSharedPtr>>> HostListsConstSharedPtr;

/**
 * A class for management of the set of hosts for a given priority level.
 */
class HostSetImpl : public HostSet {
public:
  HostSetImpl(uint32_t priority)
      : priority_(priority), hosts_(new std::vector<HostSharedPtr>()),
        healthy_hosts_(new std::vector<HostSharedPtr>()),
        hosts_per_locality_(new std::vector<std::vector<HostSharedPtr>>()),
        healthy_hosts_per_locality_(new std::vector<std::vector<HostSharedPtr>>()) {}

  void updateHosts(HostVectorConstSharedPtr hosts, HostVectorConstSharedPtr healthy_hosts,
                   HostListsConstSharedPtr hosts_per_locality,
                   HostListsConstSharedPtr healthy_hosts_per_locality,
                   const std::vector<HostSharedPtr>& hosts_added,
                   const std::vector<HostSharedPtr>& hosts_removed) override {
    hosts_ = std::move(hosts);
    healthy_hosts_ = std::move(healthy_hosts);
    hosts_per_locality_ = std::move(hosts_per_locality);
    healthy_hosts_per_locality_ = std::move(healthy_hosts_per_locality);
    runUpdateCallbacks(hosts_added, hosts_removed);
  }

  /**
   * Install a callback that will be invoked when the host set membership changes.
   * @param callback supplies the callback to invoke.
   * @return Common::CallbackHandle* the callback handle.
   */
  typedef std::function<void(uint32_t priority, const std::vector<HostSharedPtr>& hosts_added,
                             const std::vector<HostSharedPtr>& hosts_removed)>
      MemberUpdateCb;
  Common::CallbackHandle* addMemberUpdateCb(MemberUpdateCb callback) const {
    return member_update_cb_helper_.add(callback);
  }

  // Upstream::HostSet
  const std::vector<HostSharedPtr>& hosts() const override { return *hosts_; }
  const std::vector<HostSharedPtr>& healthyHosts() const override { return *healthy_hosts_; }
  const std::vector<std::vector<HostSharedPtr>>& hostsPerLocality() const override {
    return *hosts_per_locality_;
  }
  const std::vector<std::vector<HostSharedPtr>>& healthyHostsPerLocality() const override {
    return *healthy_hosts_per_locality_;
  }
  uint32_t priority() const override { return priority_; }

protected:
  virtual void runUpdateCallbacks(const std::vector<HostSharedPtr>& hosts_added,
                                  const std::vector<HostSharedPtr>& hosts_removed) {
    member_update_cb_helper_.runCallbacks(priority_, hosts_added, hosts_removed);
  }

private:
  uint32_t priority_;
  HostVectorConstSharedPtr hosts_;
  HostVectorConstSharedPtr healthy_hosts_;
  HostListsConstSharedPtr hosts_per_locality_;
  HostListsConstSharedPtr healthy_hosts_per_locality_;
  // TODO(mattklein123): Remove mutable.
  mutable Common::CallbackManager<uint32_t, const std::vector<HostSharedPtr>&,
                                  const std::vector<HostSharedPtr>&>
      member_update_cb_helper_;
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
  HostSet& getOrCreateHostSet(uint32_t priority);

protected:
  // Allows subclasses of PrioritySetImpl to create their own type of HostSetImpl.
  virtual HostSetImplPtr createHostSet(uint32_t priority) {
    return HostSetImplPtr{new HostSetImpl(priority)};
  }

private:
  virtual void runUpdateCallbacks(uint32_t priority, const std::vector<HostSharedPtr>& hosts_added,
                                  const std::vector<HostSharedPtr>& hosts_removed) {
    member_update_cb_helper_.runCallbacks(priority, hosts_added, hosts_removed);
  }
  // This vector will generally have at least one member, for priority level 0.
  // It will expand as host sets are added but currently does not shrink to
  // avoid any potential lifetime issues.
  std::vector<std::unique_ptr<HostSet>> host_sets_;
  // TODO(mattklein123): Remove mutable.
  mutable Common::CallbackManager<uint32_t, const std::vector<HostSharedPtr>&,
                                  const std::vector<HostSharedPtr>&>
      member_update_cb_helper_;
};

/**
 * Implementation of ClusterInfo that reads from JSON.
 */
class ClusterInfoImpl : public ClusterInfo,
                        public Server::Configuration::TransportSocketFactoryContext {
public:
  ClusterInfoImpl(const envoy::api::v2::Cluster& config,
                  const Network::Address::InstanceConstSharedPtr source_address,
                  Runtime::Loader& runtime, Stats::Store& stats,
                  Ssl::ContextManager& ssl_context_manager, bool added_via_api);

  static ClusterStats generateStats(Stats::Scope& scope);
  static ClusterLoadReportStats generateLoadReportStats(Stats::Scope& scope);

  // Upstream::ClusterInfo
  bool addedViaApi() const override { return added_via_api_; }
  std::chrono::milliseconds connectTimeout() const override { return connect_timeout_; }
  uint32_t perConnectionBufferLimitBytes() const override {
    return per_connection_buffer_limit_bytes_;
  }
  uint64_t features() const override { return features_; }
  const Http::Http2Settings& http2Settings() const override { return http2_settings_; }
  LoadBalancerType lbType() const override { return lb_type_; }
  envoy::api::v2::Cluster::DiscoveryType type() const override { return type_; }
  const Optional<envoy::api::v2::Cluster::RingHashLbConfig>& lbRingHashConfig() const override {
    return lb_ring_hash_config_;
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
  const envoy::api::v2::Metadata& metadata() const override { return metadata_; }

  // Server::Configuration::TransportSocketFactoryContext
  Ssl::ContextManager& sslContextManager() override { return ssl_context_manager_; }

private:
  struct ResourceManagers {
    ResourceManagers(const envoy::api::v2::Cluster& config, Runtime::Loader& runtime,
                     const std::string& cluster_name);
    ResourceManagerImplPtr load(const envoy::api::v2::Cluster& config, Runtime::Loader& runtime,
                                const std::string& cluster_name,
                                const envoy::api::v2::RoutingPriority& priority);

    typedef std::array<ResourceManagerImplPtr, NumResourcePriorities> Managers;

    Managers managers_;
  };

  static uint64_t parseFeatures(const envoy::api::v2::Cluster& config);

  Runtime::Loader& runtime_;
  const std::string name_;
  const envoy::api::v2::Cluster::DiscoveryType type_;
  const uint64_t max_requests_per_connection_;
  const std::chrono::milliseconds connect_timeout_;
  const uint32_t per_connection_buffer_limit_bytes_;
  Stats::ScopePtr stats_scope_;
  mutable ClusterStats stats_;
  Stats::IsolatedStoreImpl load_report_stats_store_;
  mutable ClusterLoadReportStats load_report_stats_;
  Network::TransportSocketFactoryPtr transport_socket_factory_;
  const uint64_t features_;
  const Http::Http2Settings http2_settings_;
  mutable ResourceManagers resource_managers_;
  const std::string maintenance_mode_runtime_key_;
  const Network::Address::InstanceConstSharedPtr source_address_;
  LoadBalancerType lb_type_;
  Optional<envoy::api::v2::Cluster::RingHashLbConfig> lb_ring_hash_config_;
  Ssl::ContextManager& ssl_context_manager_;
  const bool added_via_api_;
  LoadBalancerSubsetInfoImpl lb_subset_;
  const envoy::api::v2::Metadata metadata_;
};

/**
 * Base class all primary clusters.
 */
class ClusterImplBase : public Cluster, protected Logger::Loggable<Logger::Id::upstream> {

public:
  static ClusterSharedPtr create(const envoy::api::v2::Cluster& cluster, ClusterManager& cm,
                                 Stats::Store& stats, ThreadLocal::Instance& tls,
                                 Network::DnsResolverSharedPtr dns_resolver,
                                 Ssl::ContextManager& ssl_context_manager, Runtime::Loader& runtime,
                                 Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
                                 const LocalInfo::LocalInfo& local_info,
                                 Outlier::EventLoggerSharedPtr outlier_event_logger,
                                 bool added_via_api);
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

  // Upstream::Cluster
  HealthChecker* healthChecker() override { return health_checker_.get(); }
  ClusterInfoConstSharedPtr info() const override { return info_; }
  Outlier::Detector* outlierDetector() override { return outlier_detector_.get(); }
  const Outlier::Detector* outlierDetector() const override { return outlier_detector_.get(); }
  void initialize(std::function<void()> callback) override;

protected:
  ClusterImplBase(const envoy::api::v2::Cluster& cluster,
                  const Network::Address::InstanceConstSharedPtr source_address,
                  Runtime::Loader& runtime, Stats::Store& stats,
                  Ssl::ContextManager& ssl_context_manager, bool added_via_api);

  static HostVectorConstSharedPtr createHealthyHostList(const std::vector<HostSharedPtr>& hosts);
  static HostListsConstSharedPtr
  createHealthyHostLists(const std::vector<std::vector<HostSharedPtr>>& hosts);

  /**
   * Overridden by every concrete cluster. The cluster should do whatever pre-init is needed. E.g.,
   * query DNS, contact EDS, etc.
   */
  virtual void startPreInit() PURE;

  /**
   * Called by every concrete cluster when pre-init is complete. At this point, shared init takes
   * over and determines if there is an initial health check pass needed, etc.
   */
  void onPreInitComplete();

  static const HostListsConstSharedPtr empty_host_lists_;

  Runtime::Loader& runtime_;
  ClusterInfoConstSharedPtr
      info_; // This cluster info stores the stats scope so it must be initialized first
             // and destroyed last.
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
 * Implementation of Upstream::Cluster for static clusters (clusters that have a fixed number of
 * hosts with resolved IP addresses).
 */
class StaticClusterImpl : public ClusterImplBase {
public:
  StaticClusterImpl(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                    Stats::Store& stats, Ssl::ContextManager& ssl_context_manager,
                    ClusterManager& cm, bool added_via_api);

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  // ClusterImplBase
  void startPreInit() override;

  HostVectorSharedPtr initial_hosts_;
};

/**
 * Base for all dynamic cluster types.
 */
class BaseDynamicClusterImpl : public ClusterImplBase {
protected:
  using ClusterImplBase::ClusterImplBase;

  bool updateDynamicHostList(const std::vector<HostSharedPtr>& new_hosts,
                             std::vector<HostSharedPtr>& current_hosts,
                             std::vector<HostSharedPtr>& hosts_added,
                             std::vector<HostSharedPtr>& hosts_removed, bool depend_on_hc);
};

/**
 * Implementation of Upstream::Cluster that does periodic DNS resolution and updates the host
 * member set if the DNS members change.
 */
class StrictDnsClusterImpl : public BaseDynamicClusterImpl {
public:
  StrictDnsClusterImpl(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                       Stats::Store& stats, Ssl::ContextManager& ssl_context_manager,
                       Network::DnsResolverSharedPtr dns_resolver, ClusterManager& cm,
                       Event::Dispatcher& dispatcher, bool added_via_api);

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  struct ResolveTarget {
    ResolveTarget(StrictDnsClusterImpl& parent, Event::Dispatcher& dispatcher,
                  const std::string& url);
    ~ResolveTarget();
    void startResolve();

    StrictDnsClusterImpl& parent_;
    Network::ActiveDnsQuery* active_query_{};
    std::string dns_address_;
    uint32_t port_;
    Event::TimerPtr resolve_timer_;
    std::vector<HostSharedPtr> hosts_;
  };

  typedef std::unique_ptr<ResolveTarget> ResolveTargetPtr;

  void updateAllHosts(const std::vector<HostSharedPtr>& hosts_added,
                      const std::vector<HostSharedPtr>& hosts_removed);

  // ClusterImplBase
  void startPreInit() override;

  Network::DnsResolverSharedPtr dns_resolver_;
  std::list<ResolveTargetPtr> resolve_targets_;
  const std::chrono::milliseconds dns_refresh_rate_ms_;
  Network::DnsLookupFamily dns_lookup_family_;
};

} // namespace Upstream
} // namespace Envoy
