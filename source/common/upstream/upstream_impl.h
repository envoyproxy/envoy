#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/event/timer.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/stats/stats_impl.h"
#include "common/upstream/outlier_detection_impl.h"
#include "common/upstream/resource_manager_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Implementation of Upstream::HostDescription.
 */
class HostDescriptionImpl : virtual public HostDescription {
public:
  HostDescriptionImpl(ClusterInfoConstSharedPtr cluster, const std::string& hostname,
                      Network::Address::InstanceConstSharedPtr address, bool canary,
                      const std::string& zone)
      : cluster_(cluster), hostname_(hostname), address_(address), canary_(canary), zone_(zone),
        stats_{ALL_HOST_STATS(POOL_COUNTER(stats_store_), POOL_GAUGE(stats_store_))} {}

  // Upstream::HostDescription
  bool canary() const override { return canary_; }
  const ClusterInfo& cluster() const override { return *cluster_; }
  Outlier::DetectorHostSink& outlierDetector() const override {
    if (outlier_detector_) {
      return *outlier_detector_;
    } else {
      return null_outlier_detector_;
    }
  }
  const HostStats& stats() const override { return stats_; }
  const std::string& hostname() const override { return hostname_; }
  Network::Address::InstanceConstSharedPtr address() const override { return address_; }
  const std::string& zone() const override { return zone_; }

protected:
  ClusterInfoConstSharedPtr cluster_;
  const std::string hostname_;
  Network::Address::InstanceConstSharedPtr address_;
  const bool canary_;
  const std::string zone_;
  Stats::IsolatedStoreImpl stats_store_;
  HostStats stats_;
  Outlier::DetectorHostSinkPtr outlier_detector_;

private:
  static Outlier::DetectorHostSinkNullImpl null_outlier_detector_;
};

/**
 * Implementation of Upstream::Host.
 */
class HostImpl : public HostDescriptionImpl,
                 public Host,
                 public std::enable_shared_from_this<HostImpl> {
public:
  HostImpl(ClusterInfoConstSharedPtr cluster, const std::string& hostname,
           Network::Address::InstanceConstSharedPtr address, bool canary, uint32_t initial_weight,
           const std::string& zone)
      : HostDescriptionImpl(cluster, hostname, address, canary, zone) {
    weight(initial_weight);
  }

  // Upstream::Host
  std::list<Stats::CounterSharedPtr> counters() const override { return stats_store_.counters(); }
  CreateConnectionData createConnection(Event::Dispatcher& dispatcher) const override;
  std::list<Stats::GaugeSharedPtr> gauges() const override { return stats_store_.gauges(); }
  void healthFlagClear(HealthFlag flag) override { health_flags_ &= ~enumToInt(flag); }
  bool healthFlagGet(HealthFlag flag) const override { return health_flags_ & enumToInt(flag); }
  void healthFlagSet(HealthFlag flag) override { health_flags_ |= enumToInt(flag); }
  void setOutlierDetector(Outlier::DetectorHostSinkPtr&& outlier_detector) override {
    outlier_detector_ = std::move(outlier_detector);
  }
  bool healthy() const override { return !health_flags_; }
  uint32_t weight() const override { return weight_; }
  void weight(uint32_t new_weight) override;

protected:
  static Network::ClientConnectionPtr
  createConnection(Event::Dispatcher& dispatcher, const ClusterInfo& cluster,
                   Network::Address::InstanceConstSharedPtr address);

private:
  std::atomic<uint64_t> health_flags_{};
  std::atomic<uint32_t> weight_;
};

typedef std::shared_ptr<std::vector<HostSharedPtr>> HostVectorSharedPtr;
typedef std::shared_ptr<const std::vector<HostSharedPtr>> HostVectorConstSharedPtr;
typedef std::shared_ptr<std::vector<std::vector<HostSharedPtr>>> HostListsSharedPtr;
typedef std::shared_ptr<const std::vector<std::vector<HostSharedPtr>>> HostListsConstSharedPtr;

/**
 * Base class for all clusters as well as thread local host sets.
 */
class HostSetImpl : public virtual HostSet {
public:
  HostSetImpl()
      : hosts_(new std::vector<HostSharedPtr>()), healthy_hosts_(new std::vector<HostSharedPtr>()),
        hosts_per_zone_(new std::vector<std::vector<HostSharedPtr>>()),
        healthy_hosts_per_zone_(new std::vector<std::vector<HostSharedPtr>>()) {}

  void updateHosts(HostVectorConstSharedPtr hosts, HostVectorConstSharedPtr healthy_hosts,
                   HostListsConstSharedPtr hosts_per_zone,
                   HostListsConstSharedPtr healthy_hosts_per_zone,
                   const std::vector<HostSharedPtr>& hosts_added,
                   const std::vector<HostSharedPtr>& hosts_removed) {
    hosts_ = hosts;
    healthy_hosts_ = healthy_hosts;
    hosts_per_zone_ = hosts_per_zone;
    healthy_hosts_per_zone_ = healthy_hosts_per_zone;
    runUpdateCallbacks(hosts_added, hosts_removed);
  }

  // Upstream::HostSet
  const std::vector<HostSharedPtr>& hosts() const override { return *hosts_; }
  const std::vector<HostSharedPtr>& healthyHosts() const override { return *healthy_hosts_; }
  const std::vector<std::vector<HostSharedPtr>>& hostsPerZone() const override {
    return *hosts_per_zone_;
  }
  const std::vector<std::vector<HostSharedPtr>>& healthyHostsPerZone() const override {
    return *healthy_hosts_per_zone_;
  }
  void addMemberUpdateCb(MemberUpdateCb callback) const override;

protected:
  virtual void runUpdateCallbacks(const std::vector<HostSharedPtr>& hosts_added,
                                  const std::vector<HostSharedPtr>& hosts_removed);

private:
  HostVectorConstSharedPtr hosts_;
  HostVectorConstSharedPtr healthy_hosts_;
  HostListsConstSharedPtr hosts_per_zone_;
  HostListsConstSharedPtr healthy_hosts_per_zone_;
  mutable std::list<MemberUpdateCb> callbacks_;
};

typedef std::unique_ptr<HostSetImpl> HostSetImplPtr;

/**
 * Implementation of ClusterInfo that reads from JSON.
 */
class ClusterInfoImpl : public ClusterInfo {
public:
  ClusterInfoImpl(const Json::Object& config, Runtime::Loader& runtime, Stats::Store& stats,
                  Ssl::ContextManager& ssl_context_manager);

  static ClusterStats generateStats(Stats::Scope& scope);

  // Upstream::ClusterInfo
  std::chrono::milliseconds connectTimeout() const override { return connect_timeout_; }
  uint32_t perConnectionBufferLimitBytes() const override {
    return per_connection_buffer_limit_bytes_;
  }
  uint64_t features() const override { return features_; }
  uint64_t httpCodecOptions() const override { return http_codec_options_; }
  LoadBalancerType lbType() const override { return lb_type_; }
  bool maintenanceMode() const override;
  uint64_t maxRequestsPerConnection() const override { return max_requests_per_connection_; }
  const std::string& name() const override { return name_; }
  ResourceManager& resourceManager(ResourcePriority priority) const override;
  Ssl::ClientContext* sslContext() const override { return ssl_ctx_.get(); }
  ClusterStats& stats() const override { return stats_; }
  Stats::Scope& statsScope() const override { return *stats_scope_; }

private:
  struct ResourceManagers {
    ResourceManagers(const Json::Object& config, Runtime::Loader& runtime,
                     const std::string& cluster_name);
    ResourceManagerImplPtr load(const Json::Object& config, Runtime::Loader& runtime,
                                const std::string& cluster_name, const std::string& priority);

    typedef std::array<ResourceManagerImplPtr, NumResourcePriorities> Managers;

    Managers managers_;
  };

  static uint64_t parseFeatures(const Json::Object& config);

  Runtime::Loader& runtime_;
  const std::string name_;
  const uint64_t max_requests_per_connection_;
  const std::chrono::milliseconds connect_timeout_;
  const uint32_t per_connection_buffer_limit_bytes_;
  Stats::ScopePtr stats_scope_;
  mutable ClusterStats stats_;
  Ssl::ClientContextPtr ssl_ctx_;
  const uint64_t features_;
  const uint64_t http_codec_options_;
  mutable ResourceManagers resource_managers_;
  const std::string maintenance_mode_runtime_key_;
  LoadBalancerType lb_type_;
};

/**
 * Base class all primary clusters.
 */
class ClusterImplBase : public Cluster,
                        public HostSetImpl,
                        protected Logger::Loggable<Logger::Id::upstream> {

public:
  static ClusterPtr create(const Json::Object& cluster, ClusterManager& cm, Stats::Store& stats,
                           ThreadLocal::Instance& tls, Network::DnsResolver& dns_resolver,
                           Ssl::ContextManager& ssl_context_manager, Runtime::Loader& runtime,
                           Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
                           const Optional<SdsConfig>& sds_config,
                           const LocalInfo::LocalInfo& local_info,
                           Outlier::EventLoggerSharedPtr outlier_event_logger);

  /**
   * Optionally set the health checker for the primary cluster. This is done after cluster
   * creation since the health checker assumes that the cluster has already been fully initialized
   * so there is a cyclic dependency. However we want the cluster to own the health checker.
   */
  void setHealthChecker(HealthCheckerPtr&& health_checker);

  /**
   * Optionally set the outlier detector for the primary cluster. Done for the same reason as
   * documented in setHealthChecker().
   */
  void setOutlierDetector(Outlier::DetectorSharedPtr outlier_detector);

  // Upstream::Cluster
  ClusterInfoConstSharedPtr info() const override { return info_; }
  const Outlier::Detector* outlierDetector() const override { return outlier_detector_.get(); }

protected:
  ClusterImplBase(const Json::Object& config, Runtime::Loader& runtime, Stats::Store& stats,
                  Ssl::ContextManager& ssl_context_manager);

  static HostVectorConstSharedPtr createHealthyHostList(const std::vector<HostSharedPtr>& hosts);
  static HostListsConstSharedPtr
  createHealthyHostLists(const std::vector<std::vector<HostSharedPtr>>& hosts);
  void runUpdateCallbacks(const std::vector<HostSharedPtr>& hosts_added,
                          const std::vector<HostSharedPtr>& hosts_removed) override;

  static const HostListsConstSharedPtr empty_host_lists_;

  Runtime::Loader& runtime_;
  ClusterInfoConstSharedPtr
      info_; // This cluster info stores the stats scope so it must be initialized first
             // and destroyed last.
  HealthCheckerPtr health_checker_;
  Outlier::DetectorSharedPtr outlier_detector_;

private:
  void reloadHealthyHosts();
};

/**
 * Implementation of Upstream::Cluster for static clusters (clusters that have a fixed number of
 * hosts with resolved IP addresses).
 */
class StaticClusterImpl : public ClusterImplBase {
public:
  StaticClusterImpl(const Json::Object& config, Runtime::Loader& runtime, Stats::Store& stats,
                    Ssl::ContextManager& ssl_context_manager);

  // Upstream::Cluster
  void initialize() override {}
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }
  void setInitializedCb(std::function<void()> callback) override { callback(); }
};

/**
 * Base for all dynamic cluster types.
 */
class BaseDynamicClusterImpl : public ClusterImplBase {
public:
  // Upstream::Cluster
  void setInitializedCb(std::function<void()> callback) override {
    if (initialized_) {
      callback();
    } else {
      initialize_callback_ = callback;
    }
  }

protected:
  using ClusterImplBase::ClusterImplBase;

  bool updateDynamicHostList(const std::vector<HostSharedPtr>& new_hosts,
                             std::vector<HostSharedPtr>& current_hosts,
                             std::vector<HostSharedPtr>& hosts_added,
                             std::vector<HostSharedPtr>& hosts_removed, bool depend_on_hc);

  std::function<void()> initialize_callback_;
  // Set once the first resolve completes.
  bool initialized_ = false;
};

/**
 * Implementation of Upstream::Cluster that does periodic DNS resolution and updates the host
 * member set if the DNS members change.
 */
class StrictDnsClusterImpl : public BaseDynamicClusterImpl {
public:
  StrictDnsClusterImpl(const Json::Object& config, Runtime::Loader& runtime, Stats::Store& stats,
                       Ssl::ContextManager& ssl_context_manager, Network::DnsResolver& dns_resolver,
                       Event::Dispatcher& dispatcher);

  // Upstream::Cluster
  void initialize() override {}
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

  Network::DnsResolver& dns_resolver_;
  std::list<ResolveTargetPtr> resolve_targets_;
  const std::chrono::milliseconds dns_refresh_rate_ms_;
};

} // Upstream
} // Envoy
