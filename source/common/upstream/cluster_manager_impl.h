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
#include "envoy/config/xds_resources_delegate.h"
#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/context.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/tcp/async_tcp_client.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/cleanup.h"
#include "source/common/config/subscription_factory_impl.h"
#include "source/common/http/async_client_impl.h"
#include "source/common/http/http_server_properties_cache_impl.h"
#include "source/common/http/http_server_properties_cache_manager_impl.h"
#include "source/common/quic/quic_stat_names.h"
#include "source/common/tcp/async_tcp_client_impl.h"
#include "source/common/upstream/cluster_discovery_manager.h"
#include "source/common/upstream/host_utility.h"
#include "source/common/upstream/load_stats_reporter.h"
#include "source/common/upstream/od_cds_api_impl.h"
#include "source/common/upstream/priority_conn_pool_map.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/server/factory_context_base_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Production implementation of ClusterManagerFactory.
 */
class ProdClusterManagerFactory : public ClusterManagerFactory {
public:
  using LazyCreateDnsResolver = std::function<Network::DnsResolverSharedPtr()>;

  ProdClusterManagerFactory(Server::Configuration::ServerFactoryContext& context,
                            Stats::Store& stats, ThreadLocal::Instance& tls,
                            Http::Context& http_context, LazyCreateDnsResolver dns_resolver_fn,
                            Ssl::ContextManager& ssl_context_manager,
                            Secret::SecretManager& secret_manager,
                            Quic::QuicStatNames& quic_stat_names, const Server::Instance& server)
      : context_(context), stats_(stats), tls_(tls), http_context_(http_context),
        dns_resolver_fn_(dns_resolver_fn), ssl_context_manager_(ssl_context_manager),
        secret_manager_(secret_manager), quic_stat_names_(quic_stat_names),
        alternate_protocols_cache_manager_factory_(context.singletonManager(), tls, {context}),
        alternate_protocols_cache_manager_(alternate_protocols_cache_manager_factory_.get()),
        server_(server) {}

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
                   TimeSource& time_source, ClusterConnectivityState& state,
                   Http::PersistentQuicInfoPtr& quic_info) override;
  Tcp::ConnectionPool::InstancePtr
  allocateTcpConnPool(Event::Dispatcher& dispatcher, HostConstSharedPtr host,
                      ResourcePriority priority,
                      const Network::ConnectionSocket::OptionsSharedPtr& options,
                      Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
                      ClusterConnectivityState& state,
                      absl::optional<std::chrono::milliseconds> tcp_pool_idle_timeout) override;
  absl::StatusOr<std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>>
  clusterFromProto(const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cm,
                   Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api) override;
  CdsApiPtr createCds(const envoy::config::core::v3::ConfigSource& cds_config,
                      const xds::core::v3::ResourceLocator* cds_resources_locator,
                      ClusterManager& cm) override;
  Secret::SecretManager& secretManager() override { return secret_manager_; }
  Singleton::Manager& singletonManager() override { return context_.singletonManager(); }

protected:
  Server::Configuration::ServerFactoryContext& context_;
  Stats::Store& stats_;
  ThreadLocal::Instance& tls_;
  Http::Context& http_context_;

  LazyCreateDnsResolver dns_resolver_fn_;
  Ssl::ContextManager& ssl_context_manager_;
  Secret::SecretManager& secret_manager_;
  Quic::QuicStatNames& quic_stat_names_;
  Http::HttpServerPropertiesCacheManagerFactoryImpl alternate_protocols_cache_manager_factory_;
  Http::HttpServerPropertiesCacheManagerSharedPtr alternate_protocols_cache_manager_;
  const Server::Instance& server_;
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

  // Return true if the cluster must be ready-for-use before ADS (Aggregated Discovery Service) can
  // be initialized; will only occur if ADS is configured to use the cluster via EnvoyGrpc.
  virtual bool requiredForAds() const PURE;
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
 * All thread local cluster manager stats. @see stats_macros.h
 */
#define ALL_THREAD_LOCAL_CLUSTER_MANAGER_STATS(GAUGE) GAUGE(clusters_inflated, NeverImport)

/**
 * Struct definition for all cluster manager stats. @see stats_macros.h
 */
struct ThreadLocalClusterManagerStats {
  ALL_THREAD_LOCAL_CLUSTER_MANAGER_STATS(GENERATE_GAUGE_STRUCT)
};

/**
 * Implementation of ClusterManager that reads from a proto configuration, maintains a central
 * cluster list, as well as thread local caches of each cluster and associated connection pools.
 */
class ClusterManagerImpl : public ClusterManager,
                           public MissingClusterNotifier,
                           Logger::Loggable<Logger::Id::upstream> {
public:
  // Initializes the ClusterManagerImpl instance based on the given Bootstrap config.
  //
  // This method *must* be called prior to invoking any other methods on the class and *must* only
  // be called once. This method should be called immediately after ClusterManagerImpl construction
  // and from the same thread in which the ClusterManagerImpl was constructed.
  //
  // The initialization is separated from the constructor because lots of work, including ADS
  // initialization, is done in this method. If the contents of this method are invoked during
  // construction, a derived class cannot override any of the virtual methods and have them invoked
  // instead, since the base class's methods are used when in a base class constructor.
  //
  // This method may throw an exception.
  void init(const envoy::config::bootstrap::v3::Bootstrap& bootstrap);

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

  ClusterInfoMaps clusters() const override {
    ClusterInfoMaps clusters_maps;
    for (const auto& cluster : active_clusters_) {
      clusters_maps.active_clusters_.emplace(cluster.first, *cluster.second->cluster_);
      if (cluster.second->cluster_->info()->addedViaApi()) {
        ++clusters_maps.added_via_api_clusters_num_;
      }
    }
    for (const auto& cluster : warming_clusters_) {
      clusters_maps.warming_clusters_.emplace(cluster.first, *cluster.second->cluster_);
      if (cluster.second->cluster_->info()->addedViaApi()) {
        ++clusters_maps.added_via_api_clusters_num_;
      }
    }
    // The number of clusters that were added via API must be at most the number
    // of active clusters + number of warming clusters.
    ASSERT(clusters_maps.added_via_api_clusters_num_ <=
           clusters_maps.active_clusters_.size() + clusters_maps.warming_clusters_.size());
    return clusters_maps;
  }

  const ClusterSet& primaryClusters() override { return primary_clusters_; }
  ThreadLocalCluster* getThreadLocalCluster(absl::string_view cluster) override;

  bool removeCluster(const std::string& cluster) override;
  void shutdown() override {
    shutdown_ = true;
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

  bool isShutdown() override { return shutdown_; }

  const absl::optional<envoy::config::core::v3::BindConfig>& bindConfig() const override {
    return bind_config_;
  }

  Config::GrpcMuxSharedPtr adsMux() override { return ads_mux_; }
  Grpc::AsyncClientManager& grpcAsyncClientManager() override { return *async_client_manager_; }

  const absl::optional<std::string>& localClusterName() const override {
    return local_cluster_name_;
  }

  ClusterUpdateCallbacksHandlePtr
  addThreadLocalClusterUpdateCallbacks(ClusterUpdateCallbacks&) override;

  OdCdsApiHandlePtr
  allocateOdCdsApi(const envoy::config::core::v3::ConfigSource& odcds_config,
                   OptRef<xds::core::v3::ResourceLocator> odcds_resources_locator,
                   ProtobufMessage::ValidationVisitor& validation_visitor) override;

  ClusterManagerFactory& clusterManagerFactory() override { return factory_; }

  Config::SubscriptionFactory& subscriptionFactory() override { return *subscription_factory_; }

  void
  initializeSecondaryClusters(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) override;

  const ClusterTrafficStatNames& clusterStatNames() const override { return cluster_stat_names_; }
  const ClusterConfigUpdateStatNames& clusterConfigUpdateStatNames() const override {
    return cluster_config_update_stat_names_;
  }
  const ClusterLbStatNames& clusterLbStatNames() const override { return cluster_lb_stat_names_; }
  const ClusterEndpointStatNames& clusterEndpointStatNames() const override {
    return cluster_endpoint_stat_names_;
  }
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

  void drainConnections(const std::string& cluster,
                        DrainConnectionsHostPredicate predicate) override;

  void drainConnections(DrainConnectionsHostPredicate predicate) override;

  void checkActiveStaticCluster(const std::string& cluster) override;

  // Upstream::MissingClusterNotifier
  void notifyMissingCluster(absl::string_view name) override;

  /*
   * Return shared_ptr for common_lb_config which is stored in an ObjectSharedPool
   *
   * @param common_lb_config The config field to be stored in ObjectSharedPool
   * @return shared_ptr to the CommonLbConfig in ObjectSharedPool
   */
  std::shared_ptr<const envoy::config::cluster::v3::Cluster::CommonLbConfig> getCommonLbConfigPtr(
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_lb_config) override {
    return common_lb_config_pool_->getObject(common_lb_config);
  }

  Config::EdsResourcesCacheOptRef edsResourcesCache() override;

protected:
  // ClusterManagerImpl's constructor should not be invoked directly; create instances from the
  // clusterManagerFromProto() static method. The init() method must be called after construction.
  ClusterManagerImpl(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                     ClusterManagerFactory& factory, Stats::Store& stats,
                     ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                     const LocalInfo::LocalInfo& local_info,
                     AccessLog::AccessLogManager& log_manager,
                     Event::Dispatcher& main_thread_dispatcher, OptRef<Server::Admin> admin,
                     ProtobufMessage::ValidationContext& validation_context, Api::Api& api,
                     Http::Context& http_context, Grpc::Context& grpc_context,
                     Router::Context& router_context, const Server::Instance& server);

  virtual void postThreadLocalRemoveHosts(const Cluster& cluster, const HostVector& hosts_removed);

  // Parameters for calling postThreadLocalClusterUpdate()
  struct ThreadLocalClusterUpdateParams {
    struct PerPriority {
      PerPriority(uint32_t priority, const HostVector& hosts_added, const HostVector& hosts_removed)
          : hosts_added_(hosts_added), hosts_removed_(hosts_removed), priority_(priority) {}
      // TODO(kbaichoo): make the hosts_added_ vector const and have the
      // cluster initialization object have a stripped down version of this
      // struct.
      HostVector hosts_added_;
      const HostVector hosts_removed_;
      PrioritySet::UpdateHostsParams update_hosts_params_;
      LocalityWeightsConstSharedPtr locality_weights_;
      // Keep small members (bools and enums) at the end of class, to reduce alignment overhead.
      const uint32_t priority_;
      bool weighted_priority_health_;
      uint32_t overprovisioning_factor_;
    };

    ThreadLocalClusterUpdateParams() = default;
    ThreadLocalClusterUpdateParams(uint32_t priority, const HostVector& hosts_added,
                                   const HostVector& hosts_removed)
        : per_priority_update_params_{{priority, hosts_added, hosts_removed}} {}

    std::vector<PerPriority> per_priority_update_params_;
  };

  /**
   * A cluster initialization object (CIO) encapsulates the relevant information
   * to create a cluster inline when there is traffic to it. We can thus use the
   * CIO to deferred instantiating clusters on workers until they are used.
   */
  struct ClusterInitializationObject {
    ClusterInitializationObject(const ThreadLocalClusterUpdateParams& params,
                                ClusterInfoConstSharedPtr cluster_info,
                                LoadBalancerFactorySharedPtr load_balancer_factory,
                                HostMapConstSharedPtr map);

    ClusterInitializationObject(
        const absl::flat_hash_map<int, ThreadLocalClusterUpdateParams::PerPriority>&
            per_priority_state,
        const ThreadLocalClusterUpdateParams& update_params, ClusterInfoConstSharedPtr cluster_info,
        LoadBalancerFactorySharedPtr load_balancer_factory, HostMapConstSharedPtr map);

    absl::flat_hash_map<int, ThreadLocalClusterUpdateParams::PerPriority> per_priority_state_;
    const ClusterInfoConstSharedPtr cluster_info_;
    const LoadBalancerFactorySharedPtr load_balancer_factory_;
    const HostMapConstSharedPtr cross_priority_host_map_;
  };

  using ClusterInitializationObjectConstSharedPtr =
      std::shared_ptr<const ClusterInitializationObject>;
  using ClusterInitializationMap =
      absl::flat_hash_map<std::string, ClusterInitializationObjectConstSharedPtr>;

  /**
   * An implementation of an on-demand CDS handle. It forwards the discovery request to the cluster
   * manager that created the handle.
   *
   * It's a protected type, so unit tests can use it.
   */
  class OdCdsApiHandleImpl : public OdCdsApiHandle {
  public:
    static OdCdsApiHandlePtr create(ClusterManagerImpl& parent, OdCdsApiSharedPtr odcds) {
      return std::make_unique<OdCdsApiHandleImpl>(parent, std::move(odcds));
    }

    OdCdsApiHandleImpl(ClusterManagerImpl& parent, OdCdsApiSharedPtr odcds)
        : parent_(parent), odcds_(std::move(odcds)) {
      ASSERT(odcds_ != nullptr);
    }

    ClusterDiscoveryCallbackHandlePtr
    requestOnDemandClusterDiscovery(absl::string_view name, ClusterDiscoveryCallbackPtr callback,
                                    std::chrono::milliseconds timeout) override {
      return parent_.requestOnDemandClusterDiscovery(odcds_, std::string(name), std::move(callback),
                                                     timeout);
    }

  private:
    ClusterManagerImpl& parent_;
    OdCdsApiSharedPtr odcds_;
  };

  virtual void postThreadLocalClusterUpdate(ClusterManagerCluster& cm_cluster,
                                            ThreadLocalClusterUpdateParams&& params);

  /**
   * Notifies cluster discovery managers in each worker thread that the discovery process for the
   * cluster with a passed name has timed out.
   *
   * It's protected, so the tests can use it.
   */
  void notifyExpiredDiscovery(absl::string_view name);

  /**
   * Creates a new discovery manager in current thread and swaps it with the one in thread local
   * cluster manager. This could be used to simulate requesting a cluster from a different
   * thread. Used for tests only.
   *
   * Protected, so tests can use it.
   *
   * @return the previous cluster discovery manager.
   */
  ClusterDiscoveryManager createAndSwapClusterDiscoveryManager(std::string thread_name);

private:
  // To enable access to the protected constructor.
  friend ProdClusterManagerFactory;

  /**
   * Thread local cached cluster data. Each thread local cluster gets updates from the parent
   * central dynamic cluster (if applicable). It maintains load balancer state and any created
   * connection pools.
   */
  struct ThreadLocalClusterManagerImpl : public ThreadLocal::ThreadLocalObject,
                                         public ClusterLifecycleCallbackHandler {
    struct ConnPoolsContainer {
      ConnPoolsContainer(Event::Dispatcher& dispatcher, const HostConstSharedPtr& host)
          : host_handle_(host->acquireHandle()), pools_{std::make_shared<ConnPools>(dispatcher,
                                                                                    host)} {}

      using ConnPools = PriorityConnPoolMap<std::vector<uint8_t>, Http::ConnectionPool::Instance>;

      // Destroyed after pools.
      const HostHandlePtr host_handle_;
      // This is a shared_ptr so we can keep it alive while cleaning up.
      std::shared_ptr<ConnPools> pools_;

      // Protect from deletion while iterating through pools_. See comments and usage
      // in `ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainConnPools()`.
      bool do_not_delete_{false};
    };

    struct TcpConnPoolsContainer {
      TcpConnPoolsContainer(HostHandlePtr&& host_handle) : host_handle_(std::move(host_handle)) {}

      using ConnPools = std::map<std::vector<uint8_t>, Tcp::ConnectionPool::InstancePtr>;

      // Destroyed after pools.
      const HostHandlePtr host_handle_;
      ConnPools pools_;
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
    struct TcpConnectionsMap {
      TcpConnectionsMap(HostHandlePtr&& host_handle) : host_handle_(std::move(host_handle)) {}

      // Destroyed after pools.
      const HostHandlePtr host_handle_;
      absl::node_hash_map<Network::ClientConnection*, std::unique_ptr<TcpConnContainer>>
          connections_;
    };

    class ClusterEntry : public ThreadLocalCluster {
    public:
      ClusterEntry(ThreadLocalClusterManagerImpl& parent, ClusterInfoConstSharedPtr cluster,
                   const LoadBalancerFactorySharedPtr& lb_factory);
      ~ClusterEntry() override;

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
      Tcp::AsyncTcpClientPtr
      tcpAsyncClient(LoadBalancerContext* context,
                     Tcp::AsyncTcpClientOptionsConstSharedPtr options) override;

      // Updates the hosts in the priority set.
      void updateHosts(const std::string& name, uint32_t priority,
                       PrioritySet::UpdateHostsParams&& update_hosts_params,
                       LocalityWeightsConstSharedPtr locality_weights,
                       const HostVector& hosts_added, const HostVector& hosts_removed,
                       absl::optional<bool> weighted_priority_health,
                       absl::optional<uint32_t> overprovisioning_factor,
                       HostMapConstSharedPtr cross_priority_host_map);

      // Drains any connection pools associated with the removed hosts. All connections will be
      // closed gracefully and no new connections will be created.
      void drainConnPools(const HostVector& hosts_removed);
      // Drains any connection pools associated with the all hosts. All connections will be
      // closed gracefully and no new connections will be created.
      void drainConnPools();
      // Drain any connection pools associated with the hosts filtered by the predicate.
      void drainConnPools(DrainConnectionsHostPredicate predicate,
                          ConnectionPool::DrainBehavior behavior);

    private:
      Http::ConnectionPool::Instance*
      httpConnPoolImpl(ResourcePriority priority,
                       absl::optional<Http::Protocol> downstream_protocol,
                       LoadBalancerContext* context, bool peek);

      Tcp::ConnectionPool::Instance* tcpConnPoolImpl(ResourcePriority priority,
                                                     LoadBalancerContext* context, bool peek);

      HostConstSharedPtr chooseHost(LoadBalancerContext* context);
      HostConstSharedPtr peekAnotherHost(LoadBalancerContext* context);

      ThreadLocalClusterManagerImpl& parent_;
      PrioritySetImpl priority_set_;

      // Don't change the order of cluster_info_ and lb_factory_/lb_ as the the lb_factory_/lb_
      // may keep a reference to the cluster_info_.
      ClusterInfoConstSharedPtr cluster_info_;
      // LB factory if applicable. Not all load balancer types have a factory. LB types that have
      // a factory will create a new LB on every membership update. LB types that don't have a
      // factory will create an LB on construction and use it forever.
      LoadBalancerFactorySharedPtr lb_factory_;
      // Current active LB.
      LoadBalancerPtr lb_;
      Http::AsyncClientPtr lazy_http_async_client_;
      // Stores QUICHE specific objects which live through out the life time of the cluster and can
      // be shared across its hosts.
      Http::PersistentQuicInfoPtr quic_info_;

      // Expected override host statues. Every bit in the HostStatusSet represent an enum value
      // of envoy::config::core::v3::HealthStatus. The specific correspondence is shown below:
      //
      // * 0b000001: envoy::config::core::v3::HealthStatus::UNKNOWN
      // * 0b000010: envoy::config::core::v3::HealthStatus::HEALTHY
      // * 0b000100: envoy::config::core::v3::HealthStatus::UNHEALTHY
      // * 0b001000: envoy::config::core::v3::HealthStatus::DRAINING
      // * 0b010000: envoy::config::core::v3::HealthStatus::TIMEOUT
      // * 0b100000: envoy::config::core::v3::HealthStatus::DEGRADED
      //
      // If multiple bit fields are set, it is acceptable as long as the status of override host is
      // in any of these statuses.
      const HostUtility::HostStatusSet override_host_statuses_{};
    };

    using ClusterEntryPtr = std::unique_ptr<ClusterEntry>;

    struct LocalClusterParams {
      LoadBalancerFactorySharedPtr load_balancer_factory_;
      ClusterInfoConstSharedPtr info_;
    };

    ThreadLocalClusterManagerImpl(ClusterManagerImpl& parent, Event::Dispatcher& dispatcher,
                                  const absl::optional<LocalClusterParams>& local_cluster_params);
    ~ThreadLocalClusterManagerImpl() override;

    // Drain or close connections of host. If no drain behavior is provided then closing will
    // be immediate.
    void drainOrCloseConnPools(const HostSharedPtr& host,
                               absl::optional<ConnectionPool::DrainBehavior> drain_behavior);

    void httpConnPoolIsIdle(HostConstSharedPtr host, ResourcePriority priority,
                            const std::vector<uint8_t>& hash_key);
    void tcpConnPoolIsIdle(HostConstSharedPtr host, const std::vector<uint8_t>& hash_key);
    void removeTcpConn(const HostConstSharedPtr& host, Network::ClientConnection& connection);
    void removeHosts(const std::string& name, const HostVector& hosts_removed);
    void updateClusterMembership(const std::string& name, uint32_t priority,
                                 PrioritySet::UpdateHostsParams update_hosts_params,
                                 LocalityWeightsConstSharedPtr locality_weights,
                                 const HostVector& hosts_added, const HostVector& hosts_removed,
                                 bool weighted_priority_health, uint64_t overprovisioning_factor,
                                 HostMapConstSharedPtr cross_priority_host_map);
    void onHostHealthFailure(const HostSharedPtr& host);

    ConnPoolsContainer* getHttpConnPoolsContainer(const HostConstSharedPtr& host,
                                                  bool allocate = false);

    // Upstream::ClusterLifecycleCallbackHandler
    ClusterUpdateCallbacksHandlePtr addClusterUpdateCallbacks(ClusterUpdateCallbacks& cb) override;

    /**
     * Transparently initialize the given thread local cluster if possible using
     * the Cluster Initialization object.
     *
     * @return The ClusterEntry of the newly initialized cluster or nullptr if there
     * is no cluster deferred cluster with that name.
     */
    ClusterEntry* initializeClusterInlineIfExists(absl::string_view cluster);

    ClusterManagerImpl& parent_;
    Event::Dispatcher& thread_local_dispatcher_;
    // Known clusters will exclusively exist in either `thread_local_clusters_`
    // or `thread_local_deferred_clusters_`.
    absl::flat_hash_map<std::string, ClusterEntryPtr> thread_local_clusters_;
    // Maps from a given cluster name to the CIO for that cluster.
    ClusterInitializationMap thread_local_deferred_clusters_;

    ClusterConnectivityState cluster_manager_state_;

    // These maps are owned by the ThreadLocalClusterManagerImpl instead of the ClusterEntry
    // to prevent lifetime/ownership issues when a cluster is dynamically removed.
    absl::node_hash_map<HostConstSharedPtr, ConnPoolsContainer> host_http_conn_pool_map_;
    absl::node_hash_map<HostConstSharedPtr, TcpConnPoolsContainer> host_tcp_conn_pool_map_;
    absl::node_hash_map<HostConstSharedPtr, TcpConnectionsMap> host_tcp_conn_map_;

    std::list<Envoy::Upstream::ClusterUpdateCallbacks*> update_callbacks_;
    const PrioritySet* local_priority_set_{};
    bool destroying_{};
    ClusterDiscoveryManager cdm_;
    ThreadLocalClusterManagerStats local_stats_;

  private:
    static ThreadLocalClusterManagerStats generateStats(Stats::Scope& scope,
                                                        const std::string& thread_name);
  };

  struct ClusterData : public ClusterManagerCluster {
    ClusterData(const envoy::config::cluster::v3::Cluster& cluster_config,
                const uint64_t cluster_config_hash, const std::string& version_info,
                bool added_via_api, bool required_for_ads, ClusterSharedPtr&& cluster,
                TimeSource& time_source)
        : cluster_config_(cluster_config), config_hash_(cluster_config_hash),
          version_info_(version_info), cluster_(std::move(cluster)),
          last_updated_(time_source.systemTime()),
          added_via_api_(added_via_api), added_or_updated_{}, required_for_ads_(required_for_ads) {}

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
    bool requiredForAds() const override { return required_for_ads_; }

    const envoy::config::cluster::v3::Cluster cluster_config_;
    const uint64_t config_hash_;
    const std::string version_info_;
    // Don't change the order of cluster_ and thread_aware_lb_ as the thread_aware_lb_ may
    // keep a reference to the cluster_.
    ClusterSharedPtr cluster_;
    // Optional thread aware LB depending on the LB type. Not all clusters have one.
    ThreadAwareLoadBalancerPtr thread_aware_lb_;
    SystemTime last_updated_;
    Common::CallbackHandlePtr member_update_cb_;
    Common::CallbackHandlePtr priority_update_cb_;
    // Keep smaller fields near the end to reduce padding
    const bool added_via_api_ : 1;
    bool added_or_updated_ : 1;
    const bool required_for_ads_ : 1;
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

  /**
   * Holds a reference to an on-demand CDS to keep it alive for the duration of a cluster discovery,
   * and an expiration timer notifying worker threads about discovery timing out.
   */
  struct ClusterCreation {
    OdCdsApiSharedPtr odcds_;
    Event::TimerPtr expiration_timer_;
  };

  using ClusterCreationsMap = absl::flat_hash_map<std::string, ClusterCreation>;

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
                             bool added_via_api, bool required_for_ads, ClusterMap& cluster_map);
  void onClusterInit(ClusterManagerCluster& cluster);
  void postThreadLocalHealthFailure(const HostSharedPtr& host);
  void updateClusterCounts();
  void clusterWarmingToActive(const std::string& cluster_name);
  static void maybePreconnect(ThreadLocalClusterManagerImpl::ClusterEntry& cluster_entry,
                              const ClusterConnectivityState& cluster_manager_state,
                              std::function<ConnectionPool::Instance*()> preconnect_pool);

  ClusterDiscoveryCallbackHandlePtr
  requestOnDemandClusterDiscovery(OdCdsApiSharedPtr odcds, std::string name,
                                  ClusterDiscoveryCallbackPtr callback,
                                  std::chrono::milliseconds timeout);

  void notifyClusterDiscoveryStatus(absl::string_view name, ClusterDiscoveryStatus status);

protected:
  ClusterMap active_clusters_;
  ClusterInitializationMap cluster_initialization_map_;

private:
  /**
   * Builds the cluster initialization object for this given cluster.
   * @return a ClusterInitializationObjectSharedPtr that can be used to create
   * this cluster or nullptr if deferred cluster creation is off or the cluster
   * type is not supported.
   */
  ClusterInitializationObjectConstSharedPtr addOrUpdateClusterInitializationObjectIfSupported(
      const ThreadLocalClusterUpdateParams& params, ClusterInfoConstSharedPtr cluster_info,
      LoadBalancerFactorySharedPtr load_balancer_factory, HostMapConstSharedPtr map);

  bool deferralIsSupportedForCluster(const ClusterInfoConstSharedPtr& info) const;

  const Server::Instance& server_;
  ClusterManagerFactory& factory_;
  Runtime::Loader& runtime_;
  Stats::Store& stats_;
  ThreadLocal::TypedSlot<ThreadLocalClusterManagerImpl> tls_;
  // Contains information about ongoing on-demand cluster discoveries.
  ClusterCreationsMap pending_cluster_creations_;
  Random::RandomGenerator& random_;
  ClusterMap warming_clusters_;
  const bool deferred_cluster_creation_;
  absl::optional<envoy::config::core::v3::BindConfig> bind_config_;
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
  ProtobufMessage::ValidationContext& validation_context_;
  Router::Context& router_context_;
  ClusterTrafficStatNames cluster_stat_names_;
  ClusterConfigUpdateStatNames cluster_config_update_stat_names_;
  ClusterLbStatNames cluster_lb_stat_names_;
  ClusterEndpointStatNames cluster_endpoint_stat_names_;
  ClusterLoadReportStatNames cluster_load_report_stat_names_;
  ClusterCircuitBreakersStatNames cluster_circuit_breakers_stat_names_;
  ClusterRequestResponseSizeStatNames cluster_request_response_size_stat_names_;
  ClusterTimeoutBudgetStatNames cluster_timeout_budget_stat_names_;
  std::shared_ptr<SharedPool::ObjectSharedPool<
      const envoy::config::cluster::v3::Cluster::CommonLbConfig, MessageUtil, MessageUtil>>
      common_lb_config_pool_;

  std::unique_ptr<Config::SubscriptionFactoryImpl> subscription_factory_;
  ClusterSet primary_clusters_;

  std::unique_ptr<Config::XdsResourcesDelegate> xds_resources_delegate_;
  std::unique_ptr<Config::XdsConfigTracker> xds_config_tracker_;

  bool initialized_{};
  bool ads_mux_initialized_{};
  std::atomic<bool> shutdown_{};

  // Records the last `warming_clusters_` map size from updateClusterCounts(). This variable is
  // used for bookkeeping to run the `resume_cds_` cleanup that decrements the pause count and
  // enables the resumption of DiscoveryRequests for the Cluster type url.
  //
  // The `warming_clusters` gauge is not suitable for this purpose, because different environments
  // (e.g. mobile) may have different stats enabled, leading to the gauge not having a reliable
  // previous warming clusters size value.
  std::size_t last_recorded_warming_clusters_count_{0};
};

} // namespace Upstream
} // namespace Envoy
