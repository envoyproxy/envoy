#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/config/grpc_mux_impl.h"
#include "common/http/async_client_impl.h"
#include "common/upstream/load_stats_reporter.h"
#include "common/upstream/upstream_impl.h"

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
                            Event::Dispatcher& main_thread_dispatcher,
                            const LocalInfo::LocalInfo& local_info,
                            Secret::SecretManager& secret_manager)
      : main_thread_dispatcher_(main_thread_dispatcher), runtime_(runtime), stats_(stats),
        tls_(tls), random_(random), dns_resolver_(dns_resolver),
        ssl_context_manager_(ssl_context_manager), local_info_(local_info),
        secret_manager_(secret_manager) {}

  // Upstream::ClusterManagerFactory
  ClusterManagerPtr
  clusterManagerFromProto(const envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                          Stats::Store& stats, ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                          Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                          AccessLog::AccessLogManager& log_manager, Server::Admin& admin) override;
  Http::ConnectionPool::InstancePtr
  allocateConnPool(Event::Dispatcher& dispatcher, HostConstSharedPtr host,
                   ResourcePriority priority, Http::Protocol protocol,
                   const Network::ConnectionSocket::OptionsSharedPtr& options) override;
  ClusterSharedPtr clusterFromProto(const envoy::api::v2::Cluster& cluster, ClusterManager& cm,
                                    Outlier::EventLoggerSharedPtr outlier_event_logger,
                                    AccessLog::AccessLogManager& log_manager,
                                    bool added_via_api) override;
  CdsApiPtr createCds(const envoy::api::v2::core::ConfigSource& cds_config,
                      const absl::optional<envoy::api::v2::core::ConfigSource>& eds_config,
                      ClusterManager& cm) override;
  Secret::SecretManager& secretManager() override { return secret_manager_; }

protected:
  Event::Dispatcher& main_thread_dispatcher_;

private:
  Runtime::Loader& runtime_;
  Stats::Store& stats_;
  ThreadLocal::Instance& tls_;
  Runtime::RandomGenerator& random_;
  Network::DnsResolverSharedPtr dns_resolver_;
  Ssl::ContextManager& ssl_context_manager_;
  const LocalInfo::LocalInfo& local_info_;
  Secret::SecretManager& secret_manager_;
};

/**
 * This is a helper class used during cluster management initialization. Dealing with primary
 * clusters, secondary clusters, and CDS, is quite complicated, so this makes it easier to test.
 */
class ClusterManagerInitHelper : Logger::Loggable<Logger::Id::upstream> {
public:
  /**
   * @param per_cluster_init_callback supplies the callback to call when a cluster has itself
   *        initialized. The cluster manager can use this for post-init processing.
   */
  ClusterManagerInitHelper(const std::function<void(Cluster&)>& per_cluster_init_callback)
      : per_cluster_init_callback_(per_cluster_init_callback) {}

  enum class State {
    // Initial state. During this state all static clusters are loaded. Any phase 1 clusters
    // are immediately initialized.
    Loading,
    // During this state we wait for all static clusters to fully initialize. This requires
    // completing phase 1 clusters, initializing phase 2 clusters, and then waiting for them.
    WaitingForStaticInitialize,
    // If CDS is configured, this state tracks waiting for the first CDS response to populate
    // clusters.
    WaitingForCdsInitialize,
    // During this state, all CDS populated clusters are undergoing either phase 1 or phase 2
    // initialization.
    CdsInitialized,
    // All clusters are fully initialized.
    AllClustersInitialized
  };

  void addCluster(Cluster& cluster);
  void onStaticLoadComplete();
  void removeCluster(Cluster& cluster);
  void setCds(CdsApi* cds);
  void setInitializedCb(std::function<void()> callback);
  State state() const { return state_; }

private:
  void maybeFinishInitialize();
  void onClusterInit(Cluster& cluster);

  std::function<void(Cluster& cluster)> per_cluster_init_callback_;
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
  GAUGE  (active_clusters)                                                                         \
  GAUGE  (warming_clusters)
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
  ClusterManagerImpl(const envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                     ClusterManagerFactory& factory, Stats::Store& stats,
                     ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                     Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                     AccessLog::AccessLogManager& log_manager,
                     Event::Dispatcher& main_thread_dispatcher, Server::Admin& admin,
                     SystemTimeSource& system_time_source,
                     MonotonicTimeSource& monotonic_time_source);

  // Upstream::ClusterManager
  bool addOrUpdateCluster(const envoy::api::v2::Cluster& cluster,
                          const std::string& version_info) override;
  void setInitializedCb(std::function<void()> callback) override {
    init_helper_.setInitializedCb(callback);
  }
  ClusterInfoMap clusters() override {
    // TODO(mattklein123): Add ability to see warming clusters in admin output.
    ClusterInfoMap clusters_map;
    for (auto& cluster : active_clusters_) {
      clusters_map.emplace(cluster.first, *cluster.second->cluster_);
    }

    return clusters_map;
  }
  ThreadLocalCluster* get(const std::string& cluster) override;
  Http::ConnectionPool::Instance* httpConnPoolForCluster(const std::string& cluster,
                                                         ResourcePriority priority,
                                                         Http::Protocol protocol,
                                                         LoadBalancerContext* context) override;
  Host::CreateConnectionData tcpConnForCluster(const std::string& cluster,
                                               LoadBalancerContext* context) override;
  Http::AsyncClient& httpAsyncClientForCluster(const std::string& cluster) override;
  bool removeCluster(const std::string& cluster) override;
  void shutdown() override {
    cds_api_.reset();
    ads_mux_.reset();
    active_clusters_.clear();
  }

  const envoy::api::v2::core::BindConfig& bindConfig() const override { return bind_config_; }

  Config::GrpcMux& adsMux() override { return *ads_mux_; }
  Grpc::AsyncClientManager& grpcAsyncClientManager() override { return *async_client_manager_; }

  const std::string& localClusterName() const override { return local_cluster_name_; }

  ClusterUpdateCallbacksHandlePtr
  addThreadLocalClusterUpdateCallbacks(ClusterUpdateCallbacks&) override;

  ClusterManagerFactory& clusterManagerFactory() override { return factory_; }

private:
  /**
   * Thread local cached cluster data. Each thread local cluster gets updates from the parent
   * central dynamic cluster (if applicable). It maintains load balancer state and any created
   * connection pools.
   */
  struct ThreadLocalClusterManagerImpl : public ThreadLocal::ThreadLocalObject {
    struct ConnPoolsContainer {
      typedef std::map<std::vector<uint8_t>, Http::ConnectionPool::InstancePtr> ConnPools;

      ConnPools pools_;
      uint64_t drains_remaining_{};
    };

    // Holds an unowned reference to a connection, and watches for Closed events. If the connection
    // is closed, this container removes itself from the container that owns it.
    struct TcpConnContainer : public Network::ConnectionCallbacks, public Event::DeferredDeletable {
    public:
      TcpConnContainer(ThreadLocalClusterManagerImpl& parent, const HostConstSharedPtr& host,
                       Network::ClientConnection& connection)
          : parent_(parent), host_(host), connection_(connection) {
        connection_.addConnectionCallbacks(*this);
      }

      // Network::ConnectionCallbacks
      void onEvent(Network::ConnectionEvent event) override {
        if (event == Network::ConnectionEvent::LocalClose ||
            event == Network::ConnectionEvent::RemoteClose) {
          parent_.removeTcpConn(host_, connection_);
        }
      }
      void onAboveWriteBufferHighWatermark() override {}
      void onBelowWriteBufferLowWatermark() override {}

      ThreadLocalClusterManagerImpl& parent_;
      HostConstSharedPtr host_;
      Network::ClientConnection& connection_;
    };
    typedef std::unordered_map<Network::ClientConnection*, std::unique_ptr<TcpConnContainer>>
        TcpConnectionsMap;

    struct ClusterEntry : public ThreadLocalCluster {
      ClusterEntry(ThreadLocalClusterManagerImpl& parent, ClusterInfoConstSharedPtr cluster,
                   const LoadBalancerFactorySharedPtr& lb_factory);
      ~ClusterEntry();

      Http::ConnectionPool::Instance* connPool(ResourcePriority priority, Http::Protocol protocol,
                                               LoadBalancerContext* context);

      // Upstream::ThreadLocalCluster
      const PrioritySet& prioritySet() override { return priority_set_; }
      ClusterInfoConstSharedPtr info() override { return cluster_info_; }
      LoadBalancer& loadBalancer() override { return *lb_; }

      ThreadLocalClusterManagerImpl& parent_;
      PrioritySetImpl priority_set_;
      // LB factory if applicable. Not all load balancer types have a factory. LB types that have
      // a factory will create a new LB on every membership update. LB types that don't have a
      // factory will create an LB on construction and use it forever.
      LoadBalancerFactorySharedPtr lb_factory_;
      // Current active LB.
      LoadBalancerPtr lb_;
      ClusterInfoConstSharedPtr cluster_info_;
      Http::AsyncClientImpl http_async_client_;
    };

    typedef std::unique_ptr<ClusterEntry> ClusterEntryPtr;

    ThreadLocalClusterManagerImpl(ClusterManagerImpl& parent, Event::Dispatcher& dispatcher,
                                  const absl::optional<std::string>& local_cluster_name);
    ~ThreadLocalClusterManagerImpl();
    void drainConnPools(const HostVector& hosts);
    void drainConnPools(HostSharedPtr old_host, ConnPoolsContainer& container);
    void removeTcpConn(const HostConstSharedPtr& host, Network::ClientConnection& connection);
    static void updateClusterMembership(const std::string& name, uint32_t priority,
                                        HostVectorConstSharedPtr hosts,
                                        HostVectorConstSharedPtr healthy_hosts,
                                        HostsPerLocalityConstSharedPtr hosts_per_locality,
                                        HostsPerLocalityConstSharedPtr healthy_hosts_per_locality,
                                        LocalityWeightsConstSharedPtr locality_weights,
                                        const HostVector& hosts_added,
                                        const HostVector& hosts_removed, ThreadLocal::Slot& tls);
    static void onHostHealthFailure(const HostSharedPtr& host, ThreadLocal::Slot& tls);

    ClusterManagerImpl& parent_;
    Event::Dispatcher& thread_local_dispatcher_;
    std::unordered_map<std::string, ClusterEntryPtr> thread_local_clusters_;

    // These maps are owned by the ThreadLocalClusterManagerImpl instead of the ClusterEntry
    // to prevent lifetime/ownership issues when a cluster is dynamically removed.
    std::unordered_map<HostConstSharedPtr, ConnPoolsContainer> host_http_conn_pool_map_;
    std::unordered_map<HostConstSharedPtr, TcpConnectionsMap> host_tcp_conn_map_;

    std::list<Envoy::Upstream::ClusterUpdateCallbacks*> update_callbacks_;
    const PrioritySet* local_priority_set_{};
    bool destroying_{};
  };

  struct ClusterData {
    ClusterData(const envoy::api::v2::Cluster& cluster_config, const std::string& version_info,
                bool added_via_api, ClusterSharedPtr&& cluster,
                SystemTimeSource& system_time_source)
        : cluster_config_(cluster_config), config_hash_(MessageUtil::hash(cluster_config)),
          version_info_(version_info), added_via_api_(added_via_api), cluster_(std::move(cluster)),
          last_updated_(system_time_source.currentTime()) {}

    bool blockUpdate(uint64_t hash) { return !added_via_api_ || config_hash_ == hash; }

    LoadBalancerFactorySharedPtr loadBalancerFactory() {
      if (thread_aware_lb_ != nullptr) {
        return thread_aware_lb_->factory();
      } else {
        return nullptr;
      }
    }

    const envoy::api::v2::Cluster cluster_config_;
    const uint64_t config_hash_;
    const std::string version_info_;
    const bool added_via_api_;
    ClusterSharedPtr cluster_;
    // Optional thread aware LB depending on the LB type. Not all clusters have one.
    ThreadAwareLoadBalancerPtr thread_aware_lb_;
    SystemTime last_updated_;
  };

  struct ClusterUpdateCallbacksHandleImpl : public ClusterUpdateCallbacksHandle {
    ClusterUpdateCallbacksHandleImpl(ClusterUpdateCallbacks& cb,
                                     std::list<ClusterUpdateCallbacks*>& parent);
    ~ClusterUpdateCallbacksHandleImpl() override;

  private:
    std::list<ClusterUpdateCallbacks*>::iterator entry;
    std::list<ClusterUpdateCallbacks*>& list;
  };

  typedef std::unique_ptr<ClusterData> ClusterDataPtr;
  // This map is ordered so that config dumping is consistent.
  typedef std::map<std::string, ClusterDataPtr> ClusterMap;

  void createOrUpdateThreadLocalCluster(ClusterData& cluster);
  ProtobufTypes::MessagePtr dumpClusterConfigs();
  static ClusterManagerStats generateStats(Stats::Scope& scope);
  void loadCluster(const envoy::api::v2::Cluster& cluster, const std::string& version_info,
                   bool added_via_api, ClusterMap& cluster_map);
  void onClusterInit(Cluster& cluster);
  void postThreadLocalClusterUpdate(const Cluster& cluster, uint32_t priority,
                                    const HostVector& hosts_added, const HostVector& hosts_removed);
  void postThreadLocalHealthFailure(const HostSharedPtr& host);
  void updateGauges();

  ClusterManagerFactory& factory_;
  Runtime::Loader& runtime_;
  Stats::Store& stats_;
  ThreadLocal::SlotPtr tls_;
  Runtime::RandomGenerator& random_;
  AccessLog::AccessLogManager& log_manager_;
  ClusterMap active_clusters_;
  ClusterMap warming_clusters_;
  absl::optional<envoy::api::v2::core::ConfigSource> eds_config_;
  envoy::api::v2::core::BindConfig bind_config_;
  Outlier::EventLoggerSharedPtr outlier_event_logger_;
  const LocalInfo::LocalInfo& local_info_;
  CdsApiPtr cds_api_;
  ClusterManagerStats cm_stats_;
  ClusterManagerInitHelper init_helper_;
  Config::GrpcMuxPtr ads_mux_;
  LoadStatsReporterPtr load_stats_reporter_;
  // The name of the local cluster of this Envoy instance if defined, else the empty string.
  std::string local_cluster_name_;
  Grpc::AsyncClientManagerPtr async_client_manager_;
  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;
  SystemTimeSource& system_time_source_;
};

} // namespace Upstream
} // namespace Envoy
