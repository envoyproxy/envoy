#pragma once

#include "sds.h"

#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/async_client_impl.h"
#include "common/json/json_loader.h"

namespace Upstream {

/**
 * Production implementation of ClusterManagerFactory.
 */
class ProdClusterManagerFactory : public ClusterManagerFactory {
public:
  ProdClusterManagerFactory(Runtime::Loader& runtime, Stats::Store& stats,
                            ThreadLocal::Instance& tls, Runtime::RandomGenerator& random,
                            Network::DnsResolver& dns_resolver,
                            Ssl::ContextManager& ssl_context_manager,
                            Event::Dispatcher& primary_dispatcher,
                            const LocalInfo::LocalInfo& local_info)
      : runtime_(runtime), stats_(stats), tls_(tls), random_(random), dns_resolver_(dns_resolver),
        ssl_context_manager_(ssl_context_manager), primary_dispatcher_(primary_dispatcher),
        local_info_(local_info) {}

  // Upstream::ClusterManagerFactory
  Http::ConnectionPool::InstancePtr allocateConnPool(Event::Dispatcher& dispatcher,
                                                     ConstHostPtr host,
                                                     ResourcePriority priority) override;
  ClusterPtr clusterFromJson(const Json::Object& cluster, ClusterManager& cm,
                             const Optional<SdsConfig>& sds_config,
                             Outlier::EventLoggerPtr outlier_event_logger) override;
  CdsApiPtr createCds(const Json::Object& config, ClusterManager& cm) override;

private:
  Runtime::Loader& runtime_;
  Stats::Store& stats_;
  ThreadLocal::Instance& tls_;
  Runtime::RandomGenerator& random_;
  Network::DnsResolver& dns_resolver_;
  Ssl::ContextManager& ssl_context_manager_;
  Event::Dispatcher& primary_dispatcher_;
  const LocalInfo::LocalInfo& local_info_;
};

/**
 * This is a helper class used during cluster management initialization. Dealing with primary
 * clusters, secondary clusters, and CDS, is quite complicated, so this makes it easier to test.
 */
class ClusterManagerInitHelper {
public:
  void addCluster(Cluster& cluster);
  void onStaticLoadComplete();
  void removeCluster(Cluster& cluster);
  void setCds(CdsApi* cds);
  void setInitializedCb(std::function<void()> callback);

private:
  enum class State {
    Loading,
    WaitingForStaticInitialize,
    WaitingForCdsInitialize,
    CdsInitialized,
    AllClustersInitialized
  };

  void maybeFinishInitialize();

  CdsApi* cds_{};
  std::function<void()> initialized_callback_;
  std::list<Cluster*> primary_init_clusters_;
  std::list<Cluster*> secondary_init_clusters_;
  State state_{State::Loading};
  bool started_secondary_initialize_{};
};

/**
 * All cluster manager stats. @see stats_macros.h
 */
// clang-format off
#define ALL_CLUSTER_MANAGER_STATS(COUNTER, GAUGE)                                                  \
  COUNTER(cluster_added)                                                                           \
  COUNTER(cluster_modified)                                                                        \
  COUNTER(cluster_removed)                                                                         \
  GAUGE  (total_clusters)
// clang-format on

/**
 * Struct definition for all cluster manager stats. @see stats_macros.h
 */
struct ClusterManagerStats {
  ALL_CLUSTER_MANAGER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Implementation of ClusterManager that reads from a JSON configuration, maintains a central
 * cluster list, as well as thread local caches of each cluster and associated connection pools.
 */
class ClusterManagerImpl : public ClusterManager {
public:
  ClusterManagerImpl(const Json::Object& config, ClusterManagerFactory& factory,
                     Stats::Store& stats, ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                     Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                     AccessLog::AccessLogManager& log_manager);

  // Upstream::ClusterManager
  bool addOrUpdatePrimaryCluster(const Json::Object& config) override;
  void setInitializedCb(std::function<void()> callback) override {
    init_helper_.setInitializedCb(callback);
  }
  ClusterInfoMap clusters() override {
    ClusterInfoMap clusters_map;
    for (auto& cluster : primary_clusters_) {
      clusters_map.emplace(cluster.first, *cluster.second.cluster_);
    }

    return clusters_map;
  }
  ClusterInfoPtr get(const std::string& cluster) override;
  Http::ConnectionPool::Instance* httpConnPoolForCluster(const std::string& cluster,
                                                         ResourcePriority priority) override;
  Host::CreateConnectionData tcpConnForCluster(const std::string& cluster) override;
  Http::AsyncClient& httpAsyncClientForCluster(const std::string& cluster) override;
  bool removePrimaryCluster(const std::string& cluster) override;
  void shutdown() override {
    cds_api_.reset();
    primary_clusters_.clear();
  }

private:
  /**
   * Thread local cached cluster data. Each thread local cluster gets updates from the parent
   * central dynamic cluster (if applicable). It maintains load balancer state and any created
   * connection pools.
   */
  struct ThreadLocalClusterManagerImpl : public ThreadLocal::ThreadLocalObject {
    struct ConnPoolsContainer {
      typedef std::array<Http::ConnectionPool::InstancePtr, NumResourcePriorities> ConnPools;

      ConnPools pools_;
      uint64_t drains_remaining_{};
    };

    struct ClusterEntry {
      ClusterEntry(ThreadLocalClusterManagerImpl& parent, ClusterInfoPtr cluster);
      ~ClusterEntry();

      Http::ConnectionPool::Instance* connPool(ResourcePriority priority);

      ThreadLocalClusterManagerImpl& parent_;
      HostSetImpl host_set_;
      LoadBalancerPtr lb_;
      ClusterInfoPtr cluster_info_;
      Http::AsyncClientImpl http_async_client_;
    };

    typedef std::unique_ptr<ClusterEntry> ClusterEntryPtr;

    ThreadLocalClusterManagerImpl(ClusterManagerImpl& parent, Event::Dispatcher& dispatcher,
                                  const Optional<std::string>& local_cluster_name);
    void drainConnPools(const std::vector<HostPtr>& hosts);
    void drainConnPools(HostPtr old_host, ConnPoolsContainer& container);
    static void updateClusterMembership(const std::string& name, ConstHostVectorPtr hosts,
                                        ConstHostVectorPtr healthy_hosts,
                                        ConstHostListsPtr hosts_per_zone,
                                        ConstHostListsPtr healthy_hosts_per_zone,
                                        const std::vector<HostPtr>& hosts_added,
                                        const std::vector<HostPtr>& hosts_removed,
                                        ThreadLocal::Instance& tls, uint32_t thread_local_slot);

    // ThreadLocal::ThreadLocalObject
    void shutdown() override;

    ClusterManagerImpl& parent_;
    Event::Dispatcher& thread_local_dispatcher_;
    std::unordered_map<std::string, ClusterEntryPtr> thread_local_clusters_;
    std::unordered_map<ConstHostPtr, ConnPoolsContainer> host_http_conn_pool_map_;
    const HostSet* local_host_set_{};
  };

  struct PrimaryClusterData {
    PrimaryClusterData(uint64_t config_hash, bool added_via_api, ClusterPtr&& cluster)
        : config_hash_(config_hash), added_via_api_(added_via_api), cluster_(std::move(cluster)) {}

    const uint64_t config_hash_;
    const bool added_via_api_;
    ClusterPtr cluster_;
  };

  static ClusterManagerStats generateStats(Stats::Scope& scope);
  void loadCluster(const Json::Object& cluster, bool added_via_api);
  void postInitializeCluster(Cluster& cluster);
  void postThreadLocalClusterUpdate(const Cluster& primary_cluster,
                                    const std::vector<HostPtr>& hosts_added,
                                    const std::vector<HostPtr>& hosts_removed);

  ClusterManagerFactory& factory_;
  Runtime::Loader& runtime_;
  Stats::Store& stats_;
  ThreadLocal::Instance& tls_;
  Runtime::RandomGenerator& random_;
  uint32_t thread_local_slot_;
  std::unordered_map<std::string, PrimaryClusterData> primary_clusters_;
  Optional<SdsConfig> sds_config_;
  Outlier::EventLoggerPtr outlier_event_logger_;
  const LocalInfo::LocalInfo& local_info_;
  CdsApiPtr cds_api_;
  ClusterManagerStats cm_stats_;
  ClusterManagerInitHelper init_helper_;
};

} // Upstream
