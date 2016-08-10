#pragma once

#include "envoy/http/async_client.h"
#include "envoy/http/conn_pool.h"
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
   * Set a callback that will be invoked when all owned clusters have been initialized.
   */
  virtual void setInitializedCb(std::function<void()> callback) PURE;

  /**
   * @return std::unordered_map<std::string, ConstClusterPtr> all current clusters. These are are
   * the primary (not thread local) clusters so should just be used for stats/admin.
   */
  virtual std::unordered_map<std::string, ConstClusterPtr> clusters() PURE;

  /**
   * @return const Cluster* the primary cluster with the given name or nullptr if it does
   *         not exist.
   */
  virtual const Cluster* get(const std::string& cluster) PURE;

  /**
   * @return whether the cluster manager knows about a particular cluster by name.
   */
  virtual bool has(const std::string& cluster) PURE;

  /**
   * Allocate a load balanced HTTP connection pool for a cluster. This is *per-thread* so that
   * callers do not need to worry about per thread synchronization. The load balancing policy that
   * is used is the one defined on the cluster when it was created.
   *
   * Can return nullptr if there is no host available in the cluster or the cluster name is not
   * valid.
   */
  virtual Http::ConnectionPool::Instance* httpConnPoolForCluster(const std::string& cluster) PURE;

  /**
   * Allocate a load balanced TCP connection for a cluster. The created connection is already
   * bound to the correct *per-thread* dispatcher, so no further synchronization is needed. The
   * load balancing policy that is used is the one defined on the cluster when it was created.
   *
   * Returns both a connection and the host that backs the connection. Both can be nullptr if there
   * is no host available in the cluster or the cluster name is not valid.
   */
  virtual Host::CreateConnectionData tcpConnForCluster(const std::string& cluster) PURE;

  /**
   * Returns a client that can be used to make async HTTP calls against the given cluster.  The
   * client may be backed by a connection pool or by a multiplexed connection.
   */
  virtual Http::AsyncClientPtr httpAsyncClientForCluster(const std::string& cluster) PURE;

  /**
   * Shutdown the cluster prior to destroying connection pools and other thread local data.
   */
  virtual void shutdown() PURE;
};

} // Upstream
