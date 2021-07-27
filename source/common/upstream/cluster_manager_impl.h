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
#include "envoy/common/callback.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/context.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/cleanup.h"
#include "source/common/config/grpc_mux_impl.h"
#include "source/common/config/subscription_factory_impl.h"
#include "source/common/http/alternate_protocols_cache_impl.h"
#include "source/common/http/alternate_protocols_cache_manager_impl.h"
#include "source/common/http/async_client_impl.h"
#include "source/common/quic/quic_stat_names.h"
#include "source/common/upstream/load_stats_reporter.h"
#include "source/common/upstream/priority_conn_pool_map.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Production implementation of ClusterManagerFactory.
 */
class ProdClusterManagerFactory : public ClusterManagerFactory {
public:
  ProdClusterManagerFactory(
      Server::Admin& admin, Runtime::Loader& runtime, Stats::Store& stats,
      ThreadLocal::Instance& tls, Network::DnsResolverSharedPtr dns_resolver,
      Ssl::ContextManager& ssl_context_manager, Event::Dispatcher& main_thread_dispatcher,
      const LocalInfo::LocalInfo& local_info, Secret::SecretManager& secret_manager,
      ProtobufMessage::ValidationContext& validation_context, Api::Api& api,
      Http::Context& http_context, Grpc::Context& grpc_context, Router::Context& router_context,
      AccessLog::AccessLogManager& log_manager, Singleton::Manager& singleton_manager,
      const Server::Options& options, Quic::QuicStatNames& quic_stat_names)
      : main_thread_dispatcher_(main_thread_dispatcher), validation_context_(validation_context),
        api_(api), http_context_(http_context), grpc_context_(grpc_context),
        router_context_(router_context), admin_(admin), runtime_(runtime), stats_(stats), tls_(tls),
        dns_resolver_(dns_resolver), ssl_context_manager_(ssl_context_manager),
        local_info_(local_info), secret_manager_(secret_manager), log_manager_(log_manager),
        singleton_manager_(singleton_manager), options_(options), quic_stat_names_(quic_stat_names),
        alternate_protocols_cache_manager_factory_(singleton_manager,
                                                   main_thread_dispatcher.timeSource(), tls_),
        alternate_protocols_cache_manager_(alternate_protocols_cache_manager_factory_.get()) {}

  // Upstream::ClusterManagerFactory
  ClusterManagerPtr
  clusterManagerFromProto(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) override;
  Http::ConnectionPool::InstancePtr
  allocateConnPool(Event::Dispatcher& dispatcher, HostConstSharedPtr host,
                   ResourcePriority priority, std::vector<Http::Protocol>& protocol,
                   const absl::optional<envoy::config::core::v3::AlternateProtocolsCacheOptions>&
                       alternate_protocol_options,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                   TimeSource& time_source, ClusterConnectivityState& state) override;
  Tcp::ConnectionPool::InstancePtr
  allocateTcpConnPool(Event::Dispatcher& dispatcher, HostConstSharedPtr host,
                      ResourcePriority priority,
                      const Network::ConnectionSocket::OptionsSharedPtr& options,
                      Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
                      ClusterConnectivityState& state) override;
  std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>
  clusterFromProto(const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cm,
                   Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api) override;
  CdsApiPtr createCds(const envoy::config::core::v3::ConfigSource& cds_config,
                      const xds::core::v3::ResourceLocator* cds_resources_locator,
                      ClusterManager& cm) override;
  Secret::SecretManager& secretManager() override { return secret_manager_; }

protected:
  Event::Dispatcher& main_thread_dispatcher_;
  ProtobufMessage::ValidationContext& validation_context_;
  Api::Api& api_;
  Http::Context& http_context_;
  Grpc::Context& grpc_context_;
  Router::Context& router_context_;
  Server::Admin& admin_;
  Runtime::Loader& runtime_;
  Stats::Store& stats_;
  ThreadLocal::Instance& tls_;
  Network::DnsResolverSharedPtr dns_resolver_;
  Ssl::ContextManager& ssl_context_manager_;
  const LocalInfo::LocalInfo& local_info_;
  Secret::SecretManager& secret_manager_;
  AccessLog::AccessLogManager& log_manager_;
  Singleton::Manager& singleton_manager_;
  const Server::Options& options_;
  Quic::QuicStatNames& quic_stat_names_;
  Http::AlternateProtocolsCacheManagerFactoryImpl alternate_protocols_cache_manager_factory_;
  Http::AlternateProtocolsCacheManagerSharedPtr alternate_protocols_cache_manager_;
};

// For friend declaration in ClusterManagerInitHelper.
class ClusterManagerImpl;

/**
 * Wrapper for a cluster owned by the cluster manager. Used by both the cluster manager and the
 * cluster manager init helper which needs to pass clusters back to the cluster manager.
 */
class ClusterManagerCluster {
public:
  virtual ~ClusterManagerCluster() = default;

  // Return the underlying cluster.
  virtual Cluster& cluster() PURE;

  // Return a new load balancer factory if the cluster has one.
  virtual LoadBalancerFactorySharedPtr loadBalancerFactory() PURE;

  // Return true if a cluster has already been added or updated.
  virtual bool addedOrUpdated() PURE;

  // Set when a cluster has been added or updated. This is only called a single time for a cluster.
  virtual void setAddedOrUpdated() PURE;
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
  ClusterManagerInitHelper(
      ClusterManager& cm,
      const std::function<void(ClusterManagerCluster&)>& per_cluster_init_callback)
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

  void addCluster(ClusterManagerCluster& cluster);
  void onStaticLoadComplete();
  void removeCluster(ClusterManagerCluster& cluster);
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
  void onClusterInit(ClusterManagerCluster& cluster);

  ClusterManager& cm_;
  std::function<void(ClusterManagerCluster& cluster)> per_cluster_init_callback_;
  CdsApi* cds_{};
  ClusterManager::PrimaryClustersReadyCallback primary_clusters_initialized_callback_;
  ClusterManager::InitializationCompleteCallback initialized_callback_;
  absl::flat_hash_map<std::string, ClusterManagerCluster*> primary_init_clusters_;
  absl::flat_hash_map<std::string, ClusterManagerCluster*> secondary_init_clusters_;
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
                     const LocalInfo::LocalInfo& local_info,
                     AccessLog::AccessLogManager& log_manager,
                     Event::Dispatcher& main_thread_dispatcher, Server::Admin& admin,
                     ProtobufMessage::ValidationContext& validation_context, Api::Api& api,
                     Http::Context& http_context, Grpc::Context& grpc_context,
                     Router::Context& router_context);

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

  ClusterInfoMaps clusters() override {
    ClusterInfoMaps clusters_maps;
    for (auto& cluster : active_clusters_) {
      clusters_maps.active_clusters_.emplace(cluster.first, *cluster.second->cluster_);
    }
    for (auto& cluster : warming_clusters_) {
      clusters_maps.warming_clusters_.emplace(cluster.first, *cluster.second->cluster_);
    }
    return clusters_maps;
  }

  const ClusterSet& primaryClusters() override { return primary_clusters_; }
  ThreadLocalCluster* getThreadLocalCluster(absl::string_view cluster) override;

  bool removeCluster(const std::string& cluster) override;
  void shutdown() override {
    if (resume_cds_ != nullptr) {
      resume_cds_->cancel();
    }
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

  const ClusterStatNames& clusterStatNames() const override { return cluster_stat_names_; }
  const ClusterLoadReportStatNames& clusterLoadReportStatNames() const override {
    return cluster_load_report_stat_names_;
  }
  const ClusterCircuitBreakersStatNames& clusterCircuitBreakersStatNames() const override {
    return cluster_circuit_breakers_stat_names_;
  }
  const ClusterRequestResponseSizeStatNames& clusterRequestResponseSizeStatNames() const override {
    return cluster_request_response_size_stat_names_;
  }
  const ClusterTimeoutBudgetStatNames& clusterTimeoutBudgetStatNames() const override {
    return cluster_timeout_budget_stat_names_;
  }

protected:
  virtual void postThreadLocalDrainConnections(const Cluster& cluster,
                                               const HostVector& hosts_removed);

  // Parameters for calling postThreadLocalClusterUpdate()
  struct ThreadLocalClusterUpdateParams {
    struct PerPriority {
      PerPriority(uint32_t priority, const HostVector& hosts_added, const HostVector& hosts_removed)
          : priority_(priority), hosts_added_(hosts_added), hosts_removed_(hosts_removed) {}

      const uint32_t priority_;
      const HostVector hosts_added_;
      const HostVector hosts_removed_;
      PrioritySet::UpdateHostsParams update_hosts_params_;
      LocalityWeightsConstSharedPtr locality_weights_;
      uint32_t overprovisioning_factor_;
    };

    ThreadLocalClusterUpdateParams() = default;
    ThreadLocalClusterUpdateParams(uint32_t priority, const HostVector& hosts_added,
                                   const HostVector& hosts_removed)
        : per_priority_update_params_{{priority, hosts_added, hosts_removed}} {}

    std::vector<PerPriority> per_priority_update_params_;
  };

  virtual void postThreadLocalClusterUpdate(ClusterManagerCluster& cm_cluster,
                                            ThreadLocalClusterUpdateParams&& params);

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
      bool draining_{false};

      // Protect from deletion while iterating through pools_. See comments and usage
      // in `ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainConnPools()`.
      bool do_not_delete_{false};
    };

    struct TcpConnPoolsContainer {
      using ConnPools = std::map<std::vector<uint8_t>, Tcp::ConnectionPool::InstancePtr>;

      ConnPools pools_;
      bool draining_{false};
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
        absl::node_hash_map<Network::ClientConnection*, std::unique_ptr<TcpConnContainer>>;

    struct ClusterEntry : public ThreadLocalCluster {
      ClusterEntry(ThreadLocalClusterManagerImpl& parent, ClusterInfoConstSharedPtr cluster,
                   const LoadBalancerFactorySharedPtr& lb_factory);
      ~ClusterEntry() override;

      Http::ConnectionPool::Instance* connPool(ResourcePriority priority,
                                               absl::optional<Http::Protocol> downstream_protocol,
                                               LoadBalancerContext* context, bool peek);

      Tcp::ConnectionPool::Instance* tcpConnPool(ResourcePriority priority,
                                                 LoadBalancerContext* context, bool peek);

      void httpConnPoolIsIdle(HostConstSharedPtr host, ResourcePriority priority,
                              const std::vector<uint8_t>& hash_key);
      void tcpConnPoolIsIdle(HostConstSharedPtr host, const std::vector<uint8_t>& hash_key);

      // Upstream::ThreadLocalCluster
      const PrioritySet& prioritySet() override { return priority_set_; }
      ClusterInfoConstSharedPtr info() override { return cluster_info_; }
      LoadBalancer& loadBalancer() override { return *lb_; }
      absl::optional<HttpPoolData> httpConnPool(ResourcePriority priority,
                                                absl::optional<Http::Protocol> downstream_protocol,
                                                LoadBalancerContext* context) override;
      absl::optional<TcpPoolData> tcpConnPool(ResourcePriority priority,
                                              LoadBalancerContext* context) override;
      Host::CreateConnectionData tcpConn(LoadBalancerContext* context) override;
      Http::AsyncClient& httpAsyncClient() override;

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

    struct LocalClusterParams {
      LoadBalancerFactorySharedPtr load_balancer_factory_;
      ClusterInfoConstSharedPtr info_;
    };

    ThreadLocalClusterManagerImpl(ClusterManagerImpl& parent, Event::Dispatcher& dispatcher,
                                  const absl::optional<LocalClusterParams>& local_cluster_params);
    ~ThreadLocalClusterManagerImpl() override;
    void drainConnPools(const HostVector& hosts);
    void drainConnPools(HostSharedPtr old_host, ConnPoolsContainer& container);
    void drainTcpConnPools(TcpConnPoolsContainer& container);
    void removeTcpConn(const HostConstSharedPtr& host, Network::ClientConnection& connection);
    void removeHosts(const std::string& name, const HostVector& hosts_removed);
    void updateClusterMembership(const std::string& name, uint32_t priority,
                                 PrioritySet::UpdateHostsParams update_hosts_params,
                                 LocalityWeightsConstSharedPtr locality_weights,
                                 const HostVector& hosts_added, const HostVector& hosts_removed,
                                 uint64_t overprovisioning_factor);
    void onHostHealthFailure(const HostSharedPtr& host);

    ConnPoolsContainer* getHttpConnPoolsContainer(const HostConstSharedPtr& host,
                                                  bool allocate = false);

    ClusterManagerImpl& parent_;
    Event::Dispatcher& thread_local_dispatcher_;
    absl::flat_hash_map<std::string, ClusterEntryPtr> thread_local_clusters_;

    ClusterConnectivityState cluster_manager_state_;

    // These maps are owned by the ThreadLocalClusterManagerImpl instead of the ClusterEntry
    // to prevent lifetime/ownership issues when a cluster is dynamically removed.
    absl::node_hash_map<HostConstSharedPtr, ConnPoolsContainer> host_http_conn_pool_map_;
    absl::node_hash_map<HostConstSharedPtr, TcpConnPoolsContainer> host_tcp_conn_pool_map_;
    absl::node_hash_map<HostConstSharedPtr, TcpConnectionsMap> host_tcp_conn_map_;

    std::list<Envoy::Upstream::ClusterUpdateCallbacks*> update_callbacks_;
    const PrioritySet* local_priority_set_{};
    bool destroying_{};
  };

  struct ClusterData : public ClusterManagerCluster {
    ClusterData(const envoy::config::cluster::v3::Cluster& cluster_config,
                const uint64_t cluster_config_hash, const std::string& version_info,
                bool added_via_api, ClusterSharedPtr&& cluster, TimeSource& time_source)
        : cluster_config_(cluster_config), config_hash_(cluster_config_hash),
          version_info_(version_info), added_via_api_(added_via_api), cluster_(std::move(cluster)),
          last_updated_(time_source.systemTime()) {}

    bool blockUpdate(uint64_t hash) { return !added_via_api_ || config_hash_ == hash; }

    // ClusterManagerCluster
    Cluster& cluster() override { return *cluster_; }
    LoadBalancerFactorySharedPtr loadBalancerFactory() override {
      if (thread_aware_lb_ != nullptr) {
        return thread_aware_lb_->factory();
      } else {
        return nullptr;
      }
    }
    bool addedOrUpdated() override { return added_or_updated_; }
    void setAddedOrUpdated() override {
      ASSERT(!added_or_updated_);
      added_or_updated_ = true;
    }

    const envoy::config::cluster::v3::Cluster cluster_config_;
    const uint64_t config_hash_;
    const std::string version_info_;
    const bool added_via_api_;
    ClusterSharedPtr cluster_;
    // Optional thread aware LB depending on the LB type. Not all clusters have one.
    ThreadAwareLoadBalancerPtr thread_aware_lb_;
    SystemTime last_updated_;
    bool added_or_updated_{};
    Common::CallbackHandlePtr member_update_cb_;
    Common::CallbackHandlePtr priority_update_cb_;
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
  using PendingUpdatesByPriorityMap = absl::node_hash_map<uint32_t, PendingUpdatesPtr>;
  using PendingUpdatesByPriorityMapPtr = std::unique_ptr<PendingUpdatesByPriorityMap>;
  using ClusterUpdatesMap = absl::node_hash_map<std::string, PendingUpdatesByPriorityMapPtr>;

  void applyUpdates(ClusterManagerCluster& cluster, uint32_t priority, PendingUpdates& updates);
  bool scheduleUpdate(ClusterManagerCluster& cluster, uint32_t priority, bool mergeable,
                      const uint64_t timeout);
  ProtobufTypes::MessagePtr dumpClusterConfigs(const Matchers::StringMatcher& name_matcher);
  static ClusterManagerStats generateStats(Stats::Scope& scope);

  /**
   * @return ClusterDataPtr contains the previous cluster in the cluster_map, or
   * nullptr if cluster_map did not contain the same cluster.
   */
  ClusterDataPtr loadCluster(const envoy::config::cluster::v3::Cluster& cluster,
                             const uint64_t cluster_hash, const std::string& version_info,
                             bool added_via_api, ClusterMap& cluster_map);
  void onClusterInit(ClusterManagerCluster& cluster);
  void postThreadLocalHealthFailure(const HostSharedPtr& host);
  void updateClusterCounts();
  void clusterWarmingToActive(const std::string& cluster_name);
  static void maybePreconnect(ThreadLocalClusterManagerImpl::ClusterEntry& cluster_entry,
                              const ClusterConnectivityState& cluster_manager_state,
                              std::function<ConnectionPool::Instance*()> preconnect_pool);

  ClusterManagerFactory& factory_;
  Runtime::Loader& runtime_;
  Stats::Store& stats_;
  ThreadLocal::TypedSlot<ThreadLocalClusterManagerImpl> tls_;
  Random::RandomGenerator& random_;

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
  // Temporarily saved resume cds callback from updateClusterCounts invocation.
  Config::ScopedResume resume_cds_;
  LoadStatsReporterPtr load_stats_reporter_;
  // The name of the local cluster of this Envoy instance if defined.
  absl::optional<std::string> local_cluster_name_;
  Grpc::AsyncClientManagerPtr async_client_manager_;
  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;
  TimeSource& time_source_;
  ClusterUpdatesMap updates_map_;
  Event::Dispatcher& dispatcher_;
  Http::Context& http_context_;
  Router::Context& router_context_;
  ClusterStatNames cluster_stat_names_;
  ClusterLoadReportStatNames cluster_load_report_stat_names_;
  ClusterCircuitBreakersStatNames cluster_circuit_breakers_stat_names_;
  ClusterRequestResponseSizeStatNames cluster_request_response_size_stat_names_;
  ClusterTimeoutBudgetStatNames cluster_timeout_budget_stat_names_;

  Config::SubscriptionFactoryImpl subscription_factory_;
  ClusterSet primary_clusters_;
};

} // namespace Upstream
} // namespace Envoy
