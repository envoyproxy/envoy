#pragma once

#include "envoy/common/pure.h"
#include "envoy/http/async_client.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

/**
 * A thread local cluster instance that can be used for direct load balancing and host set
 * interactions. In general, an instance of ThreadLocalCluster can only be safely used in the
 * direct call context after it is retrieved from the cluster manager. See ClusterManager::get()
 * for more information.
 */
class ThreadLocalCluster {
public:
  virtual ~ThreadLocalCluster() = default;

  /**
   * @return const PrioritySet& the backing priority set.
   */
  virtual const PrioritySet& prioritySet() PURE;

  /**
   * @return ClusterInfoConstSharedPtr the info for this cluster. The info is safe to store beyond
   * the lifetime of the ThreadLocalCluster instance itself.
   */
  virtual ClusterInfoConstSharedPtr info() PURE;

  /**
   * @return LoadBalancer& the backing load balancer.
   */
  virtual LoadBalancer& loadBalancer() PURE;

  /**
   * Allocate a load balanced HTTP connection pool for a cluster. This is *per-thread* so that
   * callers do not need to worry about per thread synchronization. The load balancing policy that
   * is used is the one defined on the cluster when it was created.
   *
   * @param priority the connection pool priority.
   * @param downstream_protocol the downstream protocol (if one exists) to use in protocol
   *        selection.
   * @param context the optional load balancer context.
   * @return the connection pool or nullptr if there is no host available in the cluster.
   */
  virtual Http::ConnectionPool::Instance*
  httpConnPool(ResourcePriority priority, absl::optional<Http::Protocol> downstream_protocol,
               LoadBalancerContext* context) PURE;

  /**
   * Allocate a load balanced TCP connection pool for a cluster. This is *per-thread* so that
   * callers do not need to worry about per thread synchronization. The load balancing policy that
   * is used is the one defined on the cluster when it was created.
   *
   * @param priority the connection pool priority.
   * @param context the optional load balancer context.
   * @return the connection pool or nullptr if there is no host available in the cluster.
   */
  virtual Tcp::ConnectionPool::Instance* tcpConnPool(ResourcePriority priority,
                                                     LoadBalancerContext* context) PURE;

  /**
   * Allocate a load balanced TCP connection for a cluster. The created connection is already
   * bound to the correct *per-thread* dispatcher, so no further synchronization is needed. The
   * load balancing policy that is used is the one defined on the cluster when it was created.
   *
   * @param context the optional load balancer context.
   * @return both a connection and the host that backs the connection. Both can be nullptr if there
   *         is no host available in the cluster.
   */
  virtual Host::CreateConnectionData tcpConn(LoadBalancerContext* context) PURE;

  /**
   * @return a client that can be used to make async HTTP calls against the given cluster. The
   * client may be backed by a connection pool or by a multiplexed connection. The cluster manager
   * owns the client.
   */
  virtual Http::AsyncClient& httpAsyncClient() PURE;
};

using ThreadLocalClusterOptRef = absl::optional<std::reference_wrapper<ThreadLocalCluster>>;

} // namespace Upstream
} // namespace Envoy
