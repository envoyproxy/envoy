#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/cleanup.h"
#include "common/config/grpc_mux_impl.h"
#include "common/config/subscription_factory_impl.h"
#include "common/http/async_client_impl.h"
#include "common/upstream/load_stats_reporter.h"
#include "common/upstream/priority_conn_pool_map.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Production implementation of ClusterManagerFactory.
 */
class ProdClusterManagerFactory : public ClusterManagerFactory {
public:
  ProdClusterManagerFactory(
      Server::Admin& admin, Runtime::Loader& runtime, Stats::Store& stats,
      ThreadLocal::Instance& tls, Runtime::RandomGenerator& random,
      Network::DnsResolverSharedPtr dns_resolver, Ssl::ContextManager& ssl_context_manager,
      Event::Dispatcher& main_thread_dispatcher, const LocalInfo::LocalInfo& local_info,
      Secret::SecretManager& secret_manager, ProtobufMessage::ValidationContext& validation_context,
      Api::Api& api, Http::Context& http_context, Grpc::Context& grpc_context,
      AccessLog::AccessLogManager& log_manager, Singleton::Manager& singleton_manager)
      : main_thread_dispatcher_(main_thread_dispatcher), validation_context_(validation_context),
        api_(api), http_context_(http_context), grpc_context_(grpc_context), admin_(admin),
        runtime_(runtime), stats_(stats), tls_(tls), random_(random), dns_resolver_(dns_resolver),
        ssl_context_manager_(ssl_context_manager), local_info_(local_info),
        secret_manager_(secret_manager), log_manager_(log_manager),
        singleton_manager_(singleton_manager) {}

  // Upstream::ClusterManagerFactory
  ClusterManagerPtr
  clusterManagerFromProto(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) override;
  Http::ConnectionPool::InstancePtr allocateConnPool(
      Event::Dispatcher& dispatcher, HostConstSharedPtr host, ResourcePriority priority,
      Http::Protocol protocol, const Network::ConnectionSocket::OptionsSharedPtr& options,
      const Network::TransportSocketOptionsSharedPtr& transport_socket_options) override;
  Tcp::ConnectionPool::InstancePtr
  allocateTcpConnPool(Event::Dispatcher& dispatcher, HostConstSharedPtr host,
                      ResourcePriority priority,
                      const Network::ConnectionSocket::OptionsSharedPtr& options,
                      Network::TransportSocketOptionsSharedPtr transport_socket_options) override;
  std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>
  clusterFromProto(const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cm,
                   Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api) override;
  CdsApiPtr createCds(const envoy::config::core::v3::ConfigSource& cds_config,
                      ClusterManager& cm) override;
  Secret::SecretManager& secretManager() override { return secret_manager_; }

protected:
  Event::Dispatcher& main_thread_dispatcher_;
  ProtobufMessage::ValidationContext& validation_context_;
  Api::Api& api_;
  Http::Context& http_context_;
  Grpc::Context& grpc_context_;
  Server::Admin& admin_;
  Runtime::Loader& runtime_;
  Stats::Store& stats_;
  ThreadLocal::Instance& tls_;
  Runtime::RandomGenerator& random_;
  Network::DnsResolverSharedPtr dns_resolver_;
  Ssl::ContextManager& ssl_context_manager_;
  const LocalInfo::LocalInfo& local_info_;
  Secret::SecretManager& secret_manager_;
  AccessLog::AccessLogManager& log_manager_;
  Singleton::Manager& singleton_manager_;
};

// For friend declaration in ClusterManagerInitHelper.
class ClusterManagerImpl;

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
  ClusterManagerInitHelper(ClusterManager& cm,
                           const std::function<void(Cluster&)>& per_cluster_init_callback)
      : cm_(cm), per_cluster_init_callback_(per_cluster_init_callback) {}

  enum class State {
    // Initial state. During this state all static clusters are loaded. Any primary clusters
    // immediately begin initialization.
    Loading,
    // In this state cluster manager waits for all primary clusters to finish initialization.
    // This state may immediately transition to the next state iff all clusters are STATIC and
    // without health checks enabled or health checks have failed immediately, since their
    // initialization completes immediately.
    WaitingForPrimaryInitializationToComplete,
    // During this state cluster manager waits to start initializing secondary clusters. In this
    // state all primary clusters have completed initialization. Initialization of the
    // secondary clusters is started by the `initializeSecondaryClusters` method.
    WaitingToStartSecondaryInitialization,
    // In this state cluster manager waits for all secondary clusters (if configured) to finish
    // initialization. Then, if CDS is configured, this state tracks waiting for the first CDS
    // response to populate dynamically configured clusters.
    WaitingToStartCdsInitialization,
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
  void setPrimaryClustersInitializedCb(ClusterManager::PrimaryClustersReadyCallback callback);
  void setInitializedCb(ClusterManager::InitializationCompleteCallback callback);
  State state() const { return state_; }

  void startInitializingSecondaryClusters();

private:
  // To enable invariant assertions on the cluster lists.
  friend ClusterManagerImpl;

  void initializeSecondaryClusters();
  void maybeFinishInitialize();
  void onClusterInit(Cluster& cluster);

  ClusterManager& cm_;
  std::function<void(Cluster& cluster)> per_cluster_init_callback_;
  CdsApi* cds_{};
  ClusterManager::PrimaryClustersReadyCallback primary_clusters_initialized_callback_;
  ClusterManager::InitializationCompleteCallback initialized_callback_;
  std::list<Cluster*> primary_init_clusters_;
  std::list<Cluster*> secondary_init_clusters_;
  State state_{State::Loading};
  bool started_secondary_initialize_{};
};

/**
 * All cluster manager stats. @see stats_macros.h
 */
#define ALL_CLUSTER_MANAGER_STATS(COUNTER, GAUGE)                                                  \
  COUNTER(cluster_added)                                                                           \
  COUNTER(cluster_modified)                                                                        \
  COUNTER(cluster_removed)                                                                         \
  COUNTER(cluster_updated)                                                                         \
  COUNTER(cluster_updated_via_merge)                                                               \
  COUNTER(update_merge_cancelled)                                                                  \
  COUNTER(update_out_of_merge_window)                                                              \
  GAUGE(active_clusters, NeverImport)                                                              \
  GAUGE(warming_clusters, NeverImport)

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
  ClusterManagerImpl(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                     ClusterManagerFactory& factory, Stats::Store& stats,
                     ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                     Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                     AccessLog::AccessLogManager& log_manager,
                     Event::Dispatcher& main_thread_dispatcher, Server::Admin& admin,
                     ProtobufMessage::ValidationContext& validation_context, Api::Api& api,
                     Http::Context& http_context, Grpc::Context& grpc_context);

  std::size_t warmingClusterCount() const { return warming_clusters_.size(); }

  // Upstream::ClusterManager
  bool addOrUpdateCluster(const envoy::config::cluster::v3::Cluster& cluster,
                          const std::string& version_info) override;

  void setPrimaryClustersInitializedCb(PrimaryClustersReadyCallback callback) override {
    init_helper_.setPrimaryClustersInitializedCb(callback);
  }

  void setInitializedCb(InitializationCompleteCallback callback) override {
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
  const ClusterSet& primaryClusters() override { return primary_clusters_; }
  ThreadLocalCluster* get(absl::string_view cluster) override;

  using ClusterManager::httpConnPoolForCluster;

  Http::ConnectionPool::Instance*
  httpConnPoolForCluster(const std::string& cluster, ResourcePriority priority,
                         absl::optional<Http::Protocol> downstream_protocol,
                         LoadBalancerContext* context) override;
  Tcp::ConnectionPool::Instance* tcpConnPoolForCluster(const std::string& cluster,
                                                       ResourcePriority priority,
                                                       LoadBalancerContext* context) override;
  Host::CreateConnectionData tcpConnForCluster(const std::string& cluster,
                                               LoadBalancerContext* context) override;
  Http::AsyncClient& httpAsyncClientForCluster(const std::string& cluster) override;
  bool removeCluster(const std::string& cluster) override;
  void shutdown() override {
    // Make sure we destroy all potential outgoing connections before this returns.
    cds_api_.reset();
    ads_mux_.reset();
    active_clusters_.clear();
    warming_clusters_.clear();
    updateClusterCounts();
  }

  const envoy::config::core::v3::BindConfig& bindConfig() const override { return bind_config_; }

  Config::GrpcMuxSharedPtr adsMux() override { return ads_mux_; }
  Grpc::AsyncClientManager& grpcAsyncClientManager() override { return *async_client_manager_; }

  const absl::optional<std::string>& localClusterName() const override {
    return local_cluster_name_;
  }

  ClusterUpdateCallbacksHandlePtr
  addThreadLocalClusterUpdateCallbacks(ClusterUpdateCallbacks&) override;

  ClusterManagerFactory& clusterManagerFactory() override { return factory_; }

  Config::SubscriptionFactory& subscriptionFactory() override { return subscription_factory_; }

  void
  initializeSecondaryClusters(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) override;

protected:
  virtual void postThreadLocalDrainConnections(const Cluster& cluster,
                                               const HostVector& hosts_removed);
  virtual void postThreadLocalClusterUpdate(const Cluster& cluster, uint32_t priority,
                                            const HostVector& hosts_added,
                                            const HostVector& hosts_removed);

private:
  /**
   * Thread local cached cluster data. Each thread local cluster gets updates from the parent
   * central dynamic cluster (if applicable). It maintains load balancer state and any created
   * connection pools.
   */
  struct ThreadLocalClusterManagerImpl : public ThreadLocal::ThreadLocalObject {
    struct ConnPoolsContainer {
      ConnPoolsContainer(Event::Dispatcher& dispatcher, const HostConstSharedPtr& host)
          : pools_{std::make_shared<ConnPools>(dispatcher, host)} {}

      using ConnPools = PriorityConnPoolMap<std::vector<uint8_t>, Http::ConnectionPool::Instance>;

      // This is a shared_ptr so we can keep it alive while cleaning up.
      std::shared_ptr<ConnPools> pools_;
      bool ready_to_drain_{false};
      uint64_t drains_remaining_{};
    };

    struct TcpConnPoolsContainer {
      using ConnPools = std::map<std::vector<uint8_t>, Tcp::ConnectionPool::InstancePtr>;

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
    using TcpConnectionsMap =
        std::unordered_map<Network::ClientConnection*, std::unique_ptr<TcpConnContainer>>;

    struct ClusterEntry : public ThreadLocalCluster {
      ClusterEntry(ThreadLocalClusterManagerImpl& parent, ClusterInfoConstSharedPtr cluster,
                   const LoadBalancerFactorySharedPtr& lb_factory);
      ~ClusterEntry() override;

      Http::ConnectionPool::Instance* connPool(ResourcePriority priority,
                                               absl::optional<Http::Protocol> downstream_protocol,
                                               LoadBalancerContext* context);

      Tcp::ConnectionPool::Instance* tcpConnPool(ResourcePriority priority,
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

    using ClusterEntryPtr = std::unique_ptr<ClusterEntry>;

    ThreadLocalClusterManagerImpl(ClusterManagerImpl& parent, Event::Dispatcher& dispatcher,
                                  const absl::optional<std::string>& local_cluster_name);
    ~ThreadLocalClusterManagerImpl() override;
    void drainConnPools(const HostVector& hosts);
    void drainConnPools(HostSharedPtr old_host, ConnPoolsContainer& container);
    void clearContainer(HostSharedPtr old_host, ConnPoolsContainer& container);
    void drainTcpConnPools(HostSharedPtr old_host, TcpConnPoolsContainer& container);
    void removeTcpConn(const HostConstSharedPtr& host, Network::ClientConnection& connection);
    static void removeHosts(const std::string& name, const HostVector& hosts_removed,
                            ThreadLocal::Slot& tls);
    static void updateClusterMembership(const std::string& name, uint32_t priority,
                                        PrioritySet::UpdateHostsParams update_hosts_params,
                                        LocalityWeightsConstSharedPtr locality_weights,
                                        const HostVector& hosts_added,
                                        const HostVector& hosts_removed, ThreadLocal::Slot& tls,
                                        uint64_t overprovisioning_factor);
    static void onHostHealthFailure(const HostSharedPtr& host, ThreadLocal::Slot& tls);

    ConnPoolsContainer* getHttpConnPoolsContainer(const HostConstSharedPtr& host,
                                                  bool allocate = false);

    ClusterManagerImpl& parent_;
    Event::Dispatcher& thread_local_dispatcher_;
    absl::flat_hash_map<std::string, ClusterEntryPtr> thread_local_clusters_;

    // These maps are owned by the ThreadLocalClusterManagerImpl instead of the ClusterEntry
    // to prevent lifetime/ownership issues when a cluster is dynamically removed.
    std::unordered_map<HostConstSharedPtr, ConnPoolsContainer> host_http_conn_pool_map_;
    std::unordered_map<HostConstSharedPtr, TcpConnPoolsContainer> host_tcp_conn_pool_map_;
    std::unordered_map<HostConstSharedPtr, TcpConnectionsMap> host_tcp_conn_map_;

    std::list<Envoy::Upstream::ClusterUpdateCallbacks*> update_callbacks_;
    const PrioritySet* local_priority_set_{};
    bool destroying_{};
  };

  struct ClusterData {
    ClusterData(const envoy::config::cluster::v3::Cluster& cluster_config,
                const std::string& version_info, bool added_via_api, ClusterSharedPtr&& cluster,
                TimeSource& time_source)
        : cluster_config_(cluster_config), config_hash_(MessageUtil::hash(cluster_config)),
          version_info_(version_info), added_via_api_(added_via_api), cluster_(std::move(cluster)),
          last_updated_(time_source.systemTime()) {}

    bool blockUpdate(uint64_t hash) { return !added_via_api_ || config_hash_ == hash; }

    LoadBalancerFactorySharedPtr loadBalancerFactory() {
      if (thread_aware_lb_ != nullptr) {
        return thread_aware_lb_->factory();
      } else {
        return nullptr;
      }
    }

    const envoy::config::cluster::v3::Cluster cluster_config_;
    const uint64_t config_hash_;
    const std::string version_info_;
    const bool added_via_api_;
    ClusterSharedPtr cluster_;
    // Optional thread aware LB depending on the LB type. Not all clusters have one.
    ThreadAwareLoadBalancerPtr thread_aware_lb_;
    SystemTime last_updated_;
  };

  struct ClusterUpdateCallbacksHandleImpl : public ClusterUpdateCallbacksHandle,
                                            RaiiListElement<ClusterUpdateCallbacks*> {
    ClusterUpdateCallbacksHandleImpl(ClusterUpdateCallbacks& cb,
                                     std::list<ClusterUpdateCallbacks*>& parent)
        : RaiiListElement<ClusterUpdateCallbacks*>(parent, &cb) {}
  };

  using ClusterDataPtr = std::unique_ptr<ClusterData>;
  // This map is ordered so that config dumping is consistent.
  using ClusterMap = std::map<std::string, ClusterDataPtr>;

  struct PendingUpdates {
    ~PendingUpdates() { disableTimer(); }
    void enableTimer(const uint64_t timeout) {
      if (timer_ != nullptr) {
        ASSERT(!timer_->enabled());
        timer_->enableTimer(std::chrono::milliseconds(timeout));
      }
    }
    bool disableTimer() {
      if (timer_ == nullptr) {
        return false;
      }

      const bool was_enabled = timer_->enabled();
      timer_->disableTimer();
      return was_enabled;
    }

    Event::TimerPtr timer_;
    // This is default constructed to the clock's epoch:
    // https://en.cppreference.com/w/cpp/chrono/time_point/time_point
    //
    // Depending on your execution environment this value can be different.
    // When running as host process: This will usually be the computer's boot time, which means that
    // given a not very large `Cluster.CommonLbConfig.update_merge_window`, the first update will
    // trigger immediately (the expected behavior). When running in some sandboxed environment this
    // value can be set to the start time of the sandbox, which means that the delta calculated
    // between now and the start time may fall within the
    // `Cluster.CommonLbConfig.update_merge_window`, with the side effect to delay the first update.
    MonotonicTime last_updated_;
  };

  using PendingUpdatesPtr = std::unique_ptr<PendingUpdates>;
  using PendingUpdatesByPriorityMap = std::unordered_map<uint32_t, PendingUpdatesPtr>;
  using PendingUpdatesByPriorityMapPtr = std::unique_ptr<PendingUpdatesByPriorityMap>;
  using ClusterUpdatesMap = std::unordered_map<std::string, PendingUpdatesByPriorityMapPtr>;

  void applyUpdates(const Cluster& cluster, uint32_t priority, PendingUpdates& updates);
  bool scheduleUpdate(const Cluster& cluster, uint32_t priority, bool mergeable,
                      const uint64_t timeout);
  void createOrUpdateThreadLocalCluster(ClusterData& cluster);
  ProtobufTypes::MessagePtr dumpClusterConfigs();
  static ClusterManagerStats generateStats(Stats::Scope& scope);
  void loadCluster(const envoy::config::cluster::v3::Cluster& cluster,
                   const std::string& version_info, bool added_via_api, ClusterMap& cluster_map);
  void onClusterInit(Cluster& cluster);
  void postThreadLocalHealthFailure(const HostSharedPtr& host);
  void updateClusterCounts();

  ClusterManagerFactory& factory_;
  Runtime::Loader& runtime_;
  Stats::Store& stats_;
  ThreadLocal::SlotPtr tls_;
  Runtime::RandomGenerator& random_;

protected:
  ClusterMap active_clusters_;

private:
  ClusterMap warming_clusters_;
  envoy::config::core::v3::BindConfig bind_config_;
  Outlier::EventLoggerSharedPtr outlier_event_logger_;
  const LocalInfo::LocalInfo& local_info_;
  CdsApiPtr cds_api_;
  ClusterManagerStats cm_stats_;
  ClusterManagerInitHelper init_helper_;
  Config::GrpcMuxSharedPtr ads_mux_;
  LoadStatsReporterPtr load_stats_reporter_;
  // The name of the local cluster of this Envoy instance if defined.
  absl::optional<std::string> local_cluster_name_;
  Grpc::AsyncClientManagerPtr async_client_manager_;
  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;
  TimeSource& time_source_;
  ClusterUpdatesMap updates_map_;
  Event::Dispatcher& dispatcher_;
  Http::Context& http_context_;
  Config::SubscriptionFactoryImpl subscription_factory_;
  ClusterSet primary_clusters_;
};

} // namespace Upstream
} // namespace Envoy
