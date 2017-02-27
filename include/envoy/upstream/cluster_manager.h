#pragma once

#include "envoy/http/async_client.h"
#include "envoy/http/conn_pool.h"
#include "envoy/json/json_object.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/thread_local_cluster.h"
#include "envoy/upstream/upstream.h"

namespace Upstream {

/**
 * Manages connection pools and load balancing for upstream clusters. The cluster manager is
 * persistent and shared among multiple ongoing requests/connections.
 */
class ClusterManager {
public:
  virtual ~ClusterManager() {}

  /**
   * Add or update a cluster via API. The semantics of this API are:
   * 1) The hash of the config is used to determine if an already existing cluser has changed.
   *    Nothing is done if the hash matches the previously running configuration.
   * 2) Statically defined clusters (those present when Envoy starts) can not be updated via API.
   *
   * @return true if the action results in an add/update of a cluster.
   */
  virtual bool addOrUpdatePrimaryCluster(const Json::Object& config) PURE;

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
   * @return ClusterInfoPtr the thread local cluster with the given name or nullptr if it does not
   * exist. This is thread safe.
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
                                                                 LoadBalancerContext* context) PURE;

  /**
   * Allocate a load balanced TCP connection for a cluster. The created connection is already
   * bound to the correct *per-thread* dispatcher, so no further synchronization is needed. The
   * load balancing policy that is used is the one defined on the cluster when it was created.
   *
   * Returns both a connection and the host that backs the connection. Both can be nullptr if there
   * is no host available in the cluster.
   */
  virtual Host::CreateConnectionData tcpConnForCluster(const std::string& cluster) PURE;

  /**
   * Returns a client that can be used to make async HTTP calls against the given cluster. The
   * client may be backed by a connection pool or by a multiplexed connection. The cluster manager
   * owns the client.
   */
  virtual Http::AsyncClient& httpAsyncClientForCluster(const std::string& cluster) PURE;

  /**
   * Remove a primary cluster via API. Only clusters added via addOrUpdatePrimaryCluster() can
   * be removed in this manner. Statically defined clusters present when Envoy starts cannot be
   * removed.
   *
   * @return true if the action results in the removal of a cluster.
   */
  virtual bool removePrimaryCluster(const std::string& cluster) PURE;

  /**
   * Shutdown the cluster manager prior to destroying connection pools and other thread local data.
   */
  virtual void shutdown() PURE;
};

/**
 * Global configuration for any SDS clusters.
 */
struct SdsConfig {
  std::string sds_cluster_name_;
  std::chrono::milliseconds refresh_delay_;
};

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
};

typedef std::unique_ptr<CdsApi> CdsApiPtr;

/**
 * Factory for objects needed during cluster manager operation.
 */
class ClusterManagerFactory {
public:
  virtual ~ClusterManagerFactory() {}

  /**
   * Allocate an HTTP connection pool.
   */
  virtual Http::ConnectionPool::InstancePtr allocateConnPool(Event::Dispatcher& dispatcher,
                                                             ConstHostPtr host,
                                                             ResourcePriority priority) PURE;

  /**
   * Allocate a cluster from configuration JSON.
   */
  virtual ClusterPtr clusterFromJson(const Json::Object& cluster, ClusterManager& cm,
                                     const Optional<SdsConfig>& sds_config,
                                     Outlier::EventLoggerPtr outlier_event_logger) PURE;

  /**
   * Create a CDS API provider from configuration JSON.
   */
  virtual CdsApiPtr createCds(const Json::Object& config, ClusterManager& cm) PURE;
};

} // Upstream
