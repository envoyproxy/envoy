#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "envoy/access_log/access_log.h"
#include "envoy/api/v2/cds.pb.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/http/async_client.h"
#include "envoy/http/conn_pool.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/admin.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/thread_local_cluster.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

/**
 * ClusterUpdateCallbacks provide a way to exposes Cluster lifecycle events in the
 * ClusterManager.
 */
class ClusterUpdateCallbacks {
public:
  virtual ~ClusterUpdateCallbacks() {}

  /**
   * onClusterAddOrUpdate is called when a new cluster is added or an existing cluster
   * is updated in the ClusterManager.
   * @param cluster is the ThreadLocalCluster that represents the updated
   * cluster.
   */
  virtual void onClusterAddOrUpdate(ThreadLocalCluster& cluster) PURE;

  /**
   * onClusterRemoval is called when a cluster is removed; the argument is the cluster name.
   * @param cluster_name is the name of the removed cluster.
   */
  virtual void onClusterRemoval(const std::string& cluster_name) PURE;
};

/**
 * ClusterUpdateCallbacksHandle is a RAII wrapper for a ClusterUpdateCallbacks. Deleting
 * the ClusterUpdateCallbacksHandle will remove the callbacks from ClusterManager in O(1).
 */
class ClusterUpdateCallbacksHandle {
public:
  virtual ~ClusterUpdateCallbacksHandle() {}
};

typedef std::unique_ptr<ClusterUpdateCallbacksHandle> ClusterUpdateCallbacksHandlePtr;

class ClusterManagerFactory;

/**
 * Manages connection pools and load balancing for upstream clusters. The cluster manager is
 * persistent and shared among multiple ongoing requests/connections.
 */
class ClusterManager {
public:
  virtual ~ClusterManager() {}

  /**
   * Add or update a cluster via API. The semantics of this API are:
   * 1) The hash of the config is used to determine if an already existing cluster has changed.
   *    Nothing is done if the hash matches the previously running configuration.
   * 2) Statically defined clusters (those present when Envoy starts) can not be updated via API.
   *
   * @param cluster supplies the cluster configuration.
   * @param version_info supplies the xDS version of the cluster.
   * @return true if the action results in an add/update of a cluster.
   */
  virtual bool addOrUpdateCluster(const envoy::api::v2::Cluster& cluster,
                                  const std::string& version_info) PURE;

  /**
   * Set a callback that will be invoked when all owned clusters have been initialized.
   */
  virtual void setInitializedCb(std::function<void()> callback) PURE;

  typedef std::unordered_map<std::string, std::reference_wrapper<const Cluster>> ClusterInfoMap;

  /**
   * @return ClusterInfoMap all current clusters. These are the primary (not thread local)
   * clusters which should only be used for stats/admin.
   */
  virtual ClusterInfoMap clusters() PURE;

  /**
   * @return ThreadLocalCluster* the thread local cluster with the given name or nullptr if it
   * does not exist. This is thread safe.
   *
   * NOTE: The pointer returned by this function is ONLY safe to use in the context of the owning
   * call (or if the caller knows that the cluster is fully static and will never be deleted). In
   * the case of dynamic clusters, subsequent event loop iterations may invalidate this pointer.
   * If information about the cluster needs to be kept, use the ThreadLocalCluster::info() method to
   * obtain cluster information that is safe to store.
   */
  virtual ThreadLocalCluster* get(const std::string& cluster) PURE;

  /**
   * Allocate a load balanced HTTP connection pool for a cluster. This is *per-thread* so that
   * callers do not need to worry about per thread synchronization. The load balancing policy that
   * is used is the one defined on the cluster when it was created.
   *
   * Can return nullptr if there is no host available in the cluster or if the cluster does not
   * exist.
   */
  virtual Http::ConnectionPool::Instance* httpConnPoolForCluster(const std::string& cluster,
                                                                 ResourcePriority priority,
                                                                 Http::Protocol protocol,
                                                                 LoadBalancerContext* context) PURE;

  /**
   * Allocate a load balanced TCP connection pool for a cluster. This is *per-thread* so that
   * callers do not need to worry about per thread synchronization. The load balancing policy that
   * is used is the one defined on the cluster when it was created.
   *
   * Can return nullptr if there is no host available in the cluster or if the cluster does not
   * exist.
   */
  virtual Tcp::ConnectionPool::Instance* tcpConnPoolForCluster(const std::string& cluster,
                                                               ResourcePriority priority,
                                                               LoadBalancerContext* context) PURE;

  /**
   * Allocate a load balanced TCP connection for a cluster. The created connection is already
   * bound to the correct *per-thread* dispatcher, so no further synchronization is needed. The
   * load balancing policy that is used is the one defined on the cluster when it was created.
   *
   * Returns both a connection and the host that backs the connection. Both can be nullptr if there
   * is no host available in the cluster.
   */
  virtual Host::CreateConnectionData tcpConnForCluster(const std::string& cluster,
                                                       LoadBalancerContext* context) PURE;

  /**
   * Returns a client that can be used to make async HTTP calls against the given cluster. The
   * client may be backed by a connection pool or by a multiplexed connection. The cluster manager
   * owns the client.
   */
  virtual Http::AsyncClient& httpAsyncClientForCluster(const std::string& cluster) PURE;

  /**
   * Remove a cluster via API. Only clusters added via addOrUpdateCluster() can
   * be removed in this manner. Statically defined clusters present when Envoy starts cannot be
   * removed.
   *
   * @return true if the action results in the removal of a cluster.
   */
  virtual bool removeCluster(const std::string& cluster) PURE;

  /**
   * Shutdown the cluster manager prior to destroying connection pools and other thread local data.
   */
  virtual void shutdown() PURE;

  /**
   * @return const envoy::api::v2::core::BindConfig& cluster manager wide bind configuration for new
   *         upstream connections.
   */
  virtual const envoy::api::v2::core::BindConfig& bindConfig() const PURE;

  /**
   * Return a reference to the singleton ADS provider for upstream control plane muxing of xDS. This
   * is treated somewhat as a special case in ClusterManager, since it does not relate logically to
   * the management of clusters but instead is required early in ClusterManager/server
   * initialization and in various sites that need ClusterManager for xDS API interfacing.
   *
   * @return GrpcMux& ADS API provider referencee.
   */
  virtual Config::GrpcMux& adsMux() PURE;

  /**
   * @return Grpc::AsyncClientManager& the cluster manager's gRPC client manager.
   */
  virtual Grpc::AsyncClientManager& grpcAsyncClientManager() PURE;

  /**
   * Return the local cluster name, if it was configured.
   *
   * @return std::string the local cluster name, or "" if no local cluster was configured.
   */
  virtual const std::string& localClusterName() const PURE;

  /**
   * This method allows to register callbacks for cluster lifecycle events in the ClusterManager.
   * The callbacks will be registered in a thread local slot and the callbacks will be executed
   * on the thread that registered them.
   * To be executed on all threads, Callbacks need to be registered on all threads.
   *
   * @param callbacks are the ClusterUpdateCallbacks to add or remove to the cluster manager.
   * @return ClusterUpdateCallbacksHandlePtr a RAII that needs to be deleted to
   * unregister the callback.
   */
  virtual ClusterUpdateCallbacksHandlePtr
  addThreadLocalClusterUpdateCallbacks(ClusterUpdateCallbacks& callbacks) PURE;

  virtual ClusterManagerFactory& clusterManagerFactory() PURE;
};

typedef std::unique_ptr<ClusterManager> ClusterManagerPtr;

/**
 * Abstract interface for a CDS API provider.
 */
class CdsApi {
public:
  virtual ~CdsApi() {}

  /**
   * Start the first fetch of CDS data.
   */
  virtual void initialize() PURE;

  /**
   * Set a callback that will be called when the CDS API has done an initial load from the remote
   * server. If the initial load fails, the callback will also be called.
   */
  virtual void setInitializedCb(std::function<void()> callback) PURE;

  /**
   * @return std::string last accepted version from fetch.
   */
  virtual const std::string versionInfo() const PURE;
};

typedef std::unique_ptr<CdsApi> CdsApiPtr;

/**
 * Factory for objects needed during cluster manager operation.
 */
class ClusterManagerFactory {
public:
  virtual ~ClusterManagerFactory() {}

  /**
   * Allocate a cluster manager from configuration proto.
   */
  virtual ClusterManagerPtr
  clusterManagerFromProto(const envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                          Stats::Store& stats, ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                          Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                          AccessLog::AccessLogManager& log_manager, Server::Admin& admin) PURE;

  /**
   * Allocate an HTTP connection pool for the host. Pools are separated by 'priority',
   * 'protocol', and 'options->hashKey()', if any.
   */
  virtual Http::ConnectionPool::InstancePtr
  allocateConnPool(Event::Dispatcher& dispatcher, HostConstSharedPtr host,
                   ResourcePriority priority, Http::Protocol protocol,
                   const Network::ConnectionSocket::OptionsSharedPtr& options) PURE;

  /**
   * Allocate a TCP connection pool for the host. Pools are separated by 'priority' and
   * 'options->hashKey()', if any.
   */
  virtual Tcp::ConnectionPool::InstancePtr
  allocateTcpConnPool(Event::Dispatcher& dispatcher, HostConstSharedPtr host,
                      ResourcePriority priority,
                      const Network::ConnectionSocket::OptionsSharedPtr& options) PURE;

  /**
   * Allocate a cluster from configuration proto.
   */
  virtual ClusterSharedPtr clusterFromProto(const envoy::api::v2::Cluster& cluster,
                                            ClusterManager& cm,
                                            Outlier::EventLoggerSharedPtr outlier_event_logger,
                                            AccessLog::AccessLogManager& log_manager,
                                            bool added_via_api) PURE;

  /**
   * Create a CDS API provider from configuration proto.
   */
  virtual CdsApiPtr createCds(const envoy::api::v2::core::ConfigSource& cds_config,
                              const absl::optional<envoy::api::v2::core::ConfigSource>& eds_config,
                              ClusterManager& cm) PURE;

  /**
   * Returns the secret manager.
   */
  virtual Secret::SecretManager& secretManager() PURE;
};

} // namespace Upstream
} // namespace Envoy
