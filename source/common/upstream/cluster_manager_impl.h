#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/config/grpc_mux_impl.h"
#include "common/http/async_client_impl.h"
#include "common/upstream/load_stats_reporter.h"
#include "common/upstream/upstream_impl.h"

#include "api/bootstrap.pb.h"

namespace Envoy {
namespace Upstream {

/**
 * Production implementation of ClusterManagerFactory.
 */
class ProdClusterManagerFactory : public ClusterManagerFactory {
public:
  ProdClusterManagerFactory(Runtime::Loader& runtime, Stats::Store& stats,
                            ThreadLocal::Instance& tls, Runtime::RandomGenerator& random,
                            Network::DnsResolverSharedPtr dns_resolver,
                            Ssl::ContextManager& ssl_context_manager,
                            Event::Dispatcher& primary_dispatcher,
                            const LocalInfo::LocalInfo& local_info)
      : primary_dispatcher_(primary_dispatcher), runtime_(runtime), stats_(stats), tls_(tls),
        random_(random), dns_resolver_(dns_resolver), ssl_context_manager_(ssl_context_manager),
        local_info_(local_info) {}

  // Upstream::ClusterManagerFactory
  ClusterManagerPtr clusterManagerFromProto(const envoy::api::v2::Bootstrap& bootstrap,
                                            Stats::Store& stats, ThreadLocal::Instance& tls,
                                            Runtime::Loader& runtime,
                                            Runtime::RandomGenerator& random,
                                            const LocalInfo::LocalInfo& local_info,
                                            AccessLog::AccessLogManager& log_manager) override;
  Http::ConnectionPool::InstancePtr allocateConnPool(Event::Dispatcher& dispatcher,
                                                     HostConstSharedPtr host,
                                                     ResourcePriority priority) override;
  ClusterSharedPtr clusterFromProto(const envoy::api::v2::Cluster& cluster, ClusterManager& cm,
                                    Outlier::EventLoggerSharedPtr outlier_event_logger,
                                    bool added_via_api) override;
  CdsApiPtr createCds(const envoy::api::v2::ConfigSource& cds_config,
                      const Optional<envoy::api::v2::ConfigSource>& eds_config,
                      ClusterManager& cm) override;

protected:
  Event::Dispatcher& primary_dispatcher_;

private:
  Runtime::Loader& runtime_;
  Stats::Store& stats_;
  ThreadLocal::Instance& tls_;
  Runtime::RandomGenerator& random_;
  Network::DnsResolverSharedPtr dns_resolver_;
  Ssl::ContextManager& ssl_context_manager_;
  const LocalInfo::LocalInfo& local_info_;
};

/**
 * This is a helper class used during cluster management initialization. Dealing with primary
 * clusters, secondary clusters, and CDS, is quite complicated, so this makes it easier to test.
 */
class ClusterManagerInitHelper : Logger::Loggable<Logger::Id::upstream> {
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
 * Implementation of ClusterManager that reads from a proto configuration, maintains a central
 * cluster list, as well as thread local caches of each cluster and associated connection pools.
 */
class ClusterManagerImpl : public ClusterManager, Logger::Loggable<Logger::Id::upstream> {
public:
  ClusterManagerImpl(const envoy::api::v2::Bootstrap& bootstrap, ClusterManagerFactory& factory,
                     Stats::Store& stats, ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
                     Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                     AccessLog::AccessLogManager& log_manager,
                     Event::Dispatcher& primary_dispatcher);

  // Upstream::ClusterManager
  bool addOrUpdatePrimaryCluster(const envoy::api::v2::Cluster& cluster) override;
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
  ThreadLocalCluster* get(const std::string& cluster) override;
  Http::ConnectionPool::Instance* httpConnPoolForCluster(const std::string& cluster,
                                                         ResourcePriority priority,
                                                         LoadBalancerContext* context) override;
  Host::CreateConnectionData tcpConnForCluster(const std::string& cluster,
                                               LoadBalancerContext* context) override;
  Http::AsyncClient& httpAsyncClientForCluster(const std::string& cluster) override;
  bool removePrimaryCluster(const std::string& cluster) override;
  void shutdown() override {
    cds_api_.reset();
    primary_clusters_.clear();
  }

  const Network::Address::InstanceConstSharedPtr& sourceAddress() const override {
    return source_address_;
  }

  Config::GrpcMux& adsMux() override { return *ads_mux_; }

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

    struct ClusterEntry : public ThreadLocalCluster {
      ClusterEntry(ThreadLocalClusterManagerImpl& parent, ClusterInfoConstSharedPtr cluster);
      ~ClusterEntry();

      Http::ConnectionPool::Instance* connPool(ResourcePriority priority,
                                               LoadBalancerContext* context);

      // Upstream::ThreadLocalCluster
      const HostSet& hostSet() override { return host_set_; }
      ClusterInfoConstSharedPtr info() override { return cluster_info_; }
      LoadBalancer& loadBalancer() override { return *lb_; }

      ThreadLocalClusterManagerImpl& parent_;
      HostSetImpl host_set_;
      LoadBalancerPtr lb_;
      ClusterInfoConstSharedPtr cluster_info_;
      Http::AsyncClientImpl http_async_client_;
    };

    typedef std::unique_ptr<ClusterEntry> ClusterEntryPtr;

    ThreadLocalClusterManagerImpl(ClusterManagerImpl& parent, Event::Dispatcher& dispatcher,
                                  const Optional<std::string>& local_cluster_name);
    ~ThreadLocalClusterManagerImpl();
    void drainConnPools(const std::vector<HostSharedPtr>& hosts);
    void drainConnPools(HostSharedPtr old_host, ConnPoolsContainer& container);
    static void updateClusterMembership(const std::string& name, HostVectorConstSharedPtr hosts,
                                        HostVectorConstSharedPtr healthy_hosts,
                                        HostListsConstSharedPtr hosts_per_locality,
                                        HostListsConstSharedPtr healthy_hosts_per_locality,
                                        const std::vector<HostSharedPtr>& hosts_added,
                                        const std::vector<HostSharedPtr>& hosts_removed,
                                        ThreadLocal::Slot& tls);

    ClusterManagerImpl& parent_;
    Event::Dispatcher& thread_local_dispatcher_;
    std::unordered_map<std::string, ClusterEntryPtr> thread_local_clusters_;
    std::unordered_map<HostConstSharedPtr, ConnPoolsContainer> host_http_conn_pool_map_;
    const HostSet* local_host_set_{};
  };

  struct PrimaryClusterData {
    PrimaryClusterData(uint64_t config_hash, bool added_via_api, ClusterSharedPtr&& cluster)
        : config_hash_(config_hash), added_via_api_(added_via_api), cluster_(std::move(cluster)) {}

    const uint64_t config_hash_;
    const bool added_via_api_;
    ClusterSharedPtr cluster_;
  };

  static ClusterManagerStats generateStats(Stats::Scope& scope);
  void loadCluster(const envoy::api::v2::Cluster& cluster, bool added_via_api);
  void postInitializeCluster(Cluster& cluster);
  void postThreadLocalClusterUpdate(const Cluster& primary_cluster,
                                    const std::vector<HostSharedPtr>& hosts_added,
                                    const std::vector<HostSharedPtr>& hosts_removed);

  ClusterManagerFactory& factory_;
  Runtime::Loader& runtime_;
  Stats::Store& stats_;
  ThreadLocal::SlotPtr tls_;
  Runtime::RandomGenerator& random_;
  std::unordered_map<std::string, PrimaryClusterData> primary_clusters_;
  Optional<envoy::api::v2::ConfigSource> eds_config_;
  Network::Address::InstanceConstSharedPtr source_address_;
  Outlier::EventLoggerSharedPtr outlier_event_logger_;
  const LocalInfo::LocalInfo& local_info_;
  CdsApiPtr cds_api_;
  ClusterManagerStats cm_stats_;
  ClusterManagerInitHelper init_helper_;
  Config::GrpcMuxPtr ads_mux_;
  LoadStatsReporterPtr load_stats_reporter_;
};

} // namespace Upstream
} // namespace Envoy
