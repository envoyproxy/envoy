#pragma once

#include "envoy/common/pure.h"
#include "envoy/http/conn_pool.h"
#include "envoy/tcp/conn_pool.h"
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
   * Allocate or get the http connection pool which is owned by the host.
   * @param host the host owned by this cluster.
   * @param priority the priority of the connection pool.
   * @param protocol http protocol.
   * @return Http::ConnectionPool::Instance* a tcp connection pool or nullptr if there is no such pool and fail to allocate new pool.
   */
  virtual Http::ConnectionPool::Instance* getHttpPool(HostConstSharedPtr host,
                                                      ResourcePriority priority,
                                                      Http::Protocol protocol,
                                                      LoadBalancerContext* context) PURE;
  
  /**
   * Allocate or get the tcp connection pool which is owned by the host.
   * @param host the host owned by this cluster.
   * @param priority the priority of the connection pool.
   * @return Tcp::ConnectionPool::Instance* a tcp connection pool or nullptr if there is no such pool and fail to allocate new pool.
   */                                                      
  virtual Tcp::ConnectionPool::Instance*
  getTcpPool(HostConstSharedPtr host, ResourcePriority priority, LoadBalancerContext* context) PURE;
};

} // namespace Upstream
} // namespace Envoy
