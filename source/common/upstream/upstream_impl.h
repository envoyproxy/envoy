#pragma once

#include "outlier_detection_impl.h"
#include "resource_manager_impl.h"

#include "envoy/event/timer.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/json/json_loader.h"
#include "common/stats/stats_impl.h"

namespace Upstream {

/**
 * Implementation of Upstream::HostDescription.
 */
class HostDescriptionImpl : virtual public HostDescription {
public:
  HostDescriptionImpl(const Cluster& cluster, const std::string& url, bool canary,
                      const std::string& zone)
      : cluster_(cluster), url_(url), canary_(canary), zone_(zone),
        stats_{ALL_HOST_STATS(POOL_COUNTER(stats_store_), POOL_GAUGE(stats_store_))} {
    checkUrl();
  }

  // Upstream::HostDescription
  bool canary() const override { return canary_; }
  const Cluster& cluster() const override { return cluster_; }
  OutlierDetectorHostSink& outlierDetector() const override {
    if (outlier_detector_) {
      return *outlier_detector_;
    } else {
      return null_outlier_detector_;
    }
  }
  const HostStats& stats() const override { return stats_; }
  const std::string& url() const override { return url_; }
  const std::string& zone() const override { return zone_; }

protected:
  const Cluster& cluster_;
  const std::string url_;
  const bool canary_;
  const std::string zone_;
  Stats::IsolatedStoreImpl stats_store_;
  HostStats stats_;
  OutlierDetectorHostSinkPtr outlier_detector_;

private:
  void checkUrl();

  static OutlierDetectorHostSinkNullImpl null_outlier_detector_;
};

/**
 * Implementation of Upstream::Host.
 */
class HostImpl : public HostDescriptionImpl,
                 public Host,
                 public std::enable_shared_from_this<HostImpl> {
public:
  HostImpl(const Cluster& cluster, const std::string& url, bool canary, uint32_t initial_weight,
           const std::string& zone)
      : HostDescriptionImpl(cluster, url, canary, zone) {
    weight(initial_weight);
  }

  // Upstream::Host
  std::list<std::reference_wrapper<Stats::Counter>> counters() const override {
    return stats_store_.counters();
  }
  CreateConnectionData createConnection(Event::Dispatcher& dispatcher) const override;
  std::list<std::reference_wrapper<Stats::Gauge>> gauges() const override {
    return stats_store_.gauges();
  }
  void healthFlagClear(HealthFlag flag) override { health_flags_ &= ~enumToInt(flag); }
  bool healthFlagGet(HealthFlag flag) const override { return health_flags_ & enumToInt(flag); }
  void healthFlagSet(HealthFlag flag) override { health_flags_ |= enumToInt(flag); }
  void setOutlierDetector(OutlierDetectorHostSinkPtr&& outlier_detector) override {
    outlier_detector_ = std::move(outlier_detector);
  }
  bool healthy() const override { return !health_flags_; }
  uint32_t weight() const override { return weight_; }
  void weight(uint32_t new_weight);

protected:
  static Network::ClientConnectionPtr
  createConnection(Event::Dispatcher& dispatcher, const Cluster& cluster, const std::string& url);

private:
  std::atomic<uint64_t> health_flags_{};
  std::atomic<uint32_t> weight_;
};

typedef std::shared_ptr<std::vector<HostPtr>> HostVectorPtr;
typedef std::shared_ptr<const std::vector<HostPtr>> ConstHostVectorPtr;
typedef std::shared_ptr<std::vector<std::vector<HostPtr>>> HostListsPtr;
typedef std::shared_ptr<const std::vector<std::vector<HostPtr>>> ConstHostListsPtr;

/**
 * Base class for all clusters as well as thread local host sets.
 */
class HostSetImpl : public virtual HostSet {
public:
  HostSetImpl() : hosts_(new std::vector<HostPtr>()), healthy_hosts_(new std::vector<HostPtr>()) {}

  ConstHostVectorPtr rawHosts() const { return hosts_; }
  ConstHostVectorPtr rawHealthyHosts() const { return healthy_hosts_; }
  ConstHostListsPtr rawHostsPerZone() const { return hosts_per_zone_; }
  ConstHostListsPtr rawHealthyHostsPerZone() const { return healthy_hosts_per_zone_; }
  void updateHosts(ConstHostVectorPtr hosts, ConstHostVectorPtr healthy_hosts,
                   ConstHostListsPtr hosts_per_zone, ConstHostListsPtr healthy_hosts_per_zone,
                   const std::vector<HostPtr>& hosts_added,
                   const std::vector<HostPtr>& hosts_removed) {
    hosts_ = hosts;
    healthy_hosts_ = healthy_hosts;
    hosts_per_zone_ = hosts_per_zone;
    healthy_hosts_per_zone_ = healthy_hosts_per_zone;
    runUpdateCallbacks(hosts_added, hosts_removed);
  }

  // Upstream::HostSet
  const std::vector<HostPtr>& hosts() const override { return *hosts_; }
  const std::vector<HostPtr>& healthyHosts() const override { return *healthy_hosts_; }
  const std::vector<std::vector<HostPtr>>& hostsPerZone() const override {
    return *hosts_per_zone_;
  }
  const std::vector<std::vector<HostPtr>>& healthyHostsPerZone() const override {
    return *healthy_hosts_per_zone_;
  }
  void addMemberUpdateCb(MemberUpdateCb callback) const override;

protected:
  virtual void runUpdateCallbacks(const std::vector<HostPtr>& hosts_added,
                                  const std::vector<HostPtr>& hosts_removed);

private:
  ConstHostVectorPtr hosts_;
  ConstHostVectorPtr healthy_hosts_;
  ConstHostListsPtr hosts_per_zone_;
  ConstHostListsPtr healthy_hosts_per_zone_;
  mutable std::list<MemberUpdateCb> callbacks_;
};

typedef std::unique_ptr<HostSetImpl> HostSetImplPtr;

/**
 * Base class all primary clusters.
 */
class ClusterImplBase : public Cluster,
                        public HostSetImpl,
                        protected Logger::Loggable<Logger::Id::upstream> {

public:
  static ClusterStats generateStats(const std::string& prefix, Stats::Store& stats);

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
  void setOutlierDetector(OutlierDetectorPtr&& outlier_detector);

  // Upstream::Cluster
  const std::string& altStatName() const override { return alt_stat_name_; }
  std::chrono::milliseconds connectTimeout() const override { return connect_timeout_; }
  uint64_t features() const override { return features_; }
  uint64_t httpCodecOptions() const override { return http_codec_options_; }
  Ssl::ClientContext* sslContext() const override { return ssl_ctx_; }
  LoadBalancerType lbType() const override { return lb_type_; }
  bool maintenanceMode() const override;
  uint64_t maxRequestsPerConnection() const override { return max_requests_per_connection_; }
  const std::string& name() const override { return name_; }
  ResourceManager& resourceManager(ResourcePriority priority) const override;
  const std::string& statPrefix() const override { return stat_prefix_; }
  ClusterStats& stats() const override { return stats_; }

protected:
  ClusterImplBase(const Json::Object& config, Runtime::Loader& runtime, Stats::Store& stats,
                  Ssl::ContextManager& ssl_context_manager);

  static ConstHostVectorPtr createHealthyHostList(const std::vector<HostPtr>& hosts);
  static ConstHostListsPtr createHealthyHostLists(const std::vector<std::vector<HostPtr>>& hosts);
  void runUpdateCallbacks(const std::vector<HostPtr>& hosts_added,
                          const std::vector<HostPtr>& hosts_removed) override;

  static const ConstHostListsPtr empty_host_lists_;

  Runtime::Loader& runtime_;
  Ssl::ClientContext* ssl_ctx_;
  const std::string name_;
  LoadBalancerType lb_type_;
  const uint64_t max_requests_per_connection_;
  const std::chrono::milliseconds connect_timeout_;
  const std::string stat_prefix_;
  mutable ClusterStats stats_;
  HealthCheckerPtr health_checker_;
  const std::string alt_stat_name_;
  const uint64_t features_;
  OutlierDetectorPtr outlier_detector_;

private:
  struct ResourceManagers {
    ResourceManagers(const Json::Object& config, Runtime::Loader& runtime,
                     const std::string& cluster_name);
    ResourceManagerImplPtr load(const Json::Object& config, Runtime::Loader& runtime,
                                const std::string& cluster_name, const std::string& priority);

    typedef std::array<ResourceManagerImplPtr, NumResourcePriorities> Managers;

    Managers managers_;
  };

  uint64_t parseFeatures(const Json::Object& config);

  const uint64_t http_codec_options_;
  mutable ResourceManagers resource_managers_;
  const std::string maintenance_mode_runtime_key_;
};

typedef std::shared_ptr<ClusterImplBase> ClusterImplBasePtr;

/**
 * Implementation of Upstream::Cluster for static clusters (clusters that have a fixed number of
 * hosts with resolved IP addresses).
 */
class StaticClusterImpl : public ClusterImplBase {
public:
  StaticClusterImpl(const Json::Object& config, Runtime::Loader& runtime, Stats::Store& stats,
                    Ssl::ContextManager& ssl_context_manager);

  // Upstream::Cluster
  void setInitializedCb(std::function<void()> callback) override { callback(); }
  void shutdown() override {}
};

/**
 * Base for all dynamic cluster types.
 */
class BaseDynamicClusterImpl : public ClusterImplBase {
public:
  // Upstream::Cluster
  void setInitializedCb(std::function<void()> callback) override {
    initialize_callback_ = callback;
  }

protected:
  using ClusterImplBase::ClusterImplBase;

  bool updateDynamicHostList(const std::vector<HostPtr>& new_hosts,
                             std::vector<HostPtr>& current_hosts, std::vector<HostPtr>& hosts_added,
                             std::vector<HostPtr>& hosts_removed, bool depend_on_hc);

  std::function<void()> initialize_callback_;
};

/**
 * Implementation of Upstream::Cluster that does periodic DNS resolution and updates the host
 * member set if the DNS members change.
 */
class StrictDnsClusterImpl : public BaseDynamicClusterImpl {
public:
  StrictDnsClusterImpl(const Json::Object& config, Runtime::Loader& runtime, Stats::Store& stats,
                       Ssl::ContextManager& ssl_context_manager,
                       Network::DnsResolver& dns_resolver);
  ~StrictDnsClusterImpl();

  // Upstream::Cluster
  void shutdown() override {}

private:
  struct ResolveTarget {
    ResolveTarget(StrictDnsClusterImpl& parent, const std::string& url);
    void startResolve();

    StrictDnsClusterImpl& parent_;
    std::string dns_address_;
    uint32_t port_;
    Event::TimerPtr resolve_timer_;
    std::vector<HostPtr> hosts_;
  };

  typedef std::unique_ptr<ResolveTarget> ResolveTargetPtr;

  void updateAllHosts(const std::vector<HostPtr>& hosts_added,
                      const std::vector<HostPtr>& hosts_removed);

  Network::DnsResolver& dns_resolver_;
  std::list<ResolveTargetPtr> resolve_targets_;
  const std::chrono::milliseconds dns_refresh_rate_ms_;
};

} // Upstream
