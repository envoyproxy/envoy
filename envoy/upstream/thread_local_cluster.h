#pragma once

#include "envoy/common/pure.h"
#include "envoy/http/async_client.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

// HttpPoolData returns information about a given pool as well as a function
// to create streams on that pool.
class HttpPoolData {
public:
  using OnNewStreamFn = std::function<void()>;

  HttpPoolData(OnNewStreamFn on_new_stream, Http::ConnectionPool::Instance* pool)
      : on_new_stream_(on_new_stream), pool_(pool) {}

  /**
   * See documentation of Http::ConnectionPool::Instance.
   */
  Envoy::Http::ConnectionPool::Cancellable*
  newStream(Http::ResponseDecoder& response_decoder,
            Envoy::Http::ConnectionPool::Callbacks& callbacks,
            const Http::ConnectionPool::Instance::StreamOptions& stream_options) {
    on_new_stream_();
    return pool_->newStream(response_decoder, callbacks, stream_options);
  }
  bool hasActiveConnections() const { return pool_->hasActiveConnections(); };

  /**
   * See documentation of Envoy::ConnectionPool::Instance.
   */
  void addIdleCallback(ConnectionPool::Instance::IdleCb cb) { pool_->addIdleCallback(cb); };
  void drainConnections(ConnectionPool::DrainBehavior drain_behavior) {
    pool_->drainConnections(drain_behavior);
  };

  Upstream::HostDescriptionConstSharedPtr host() const { return pool_->host(); }

private:
  friend class HttpPoolDataPeer;

  OnNewStreamFn on_new_stream_;
  Http::ConnectionPool::Instance* pool_;
};

// Tcp pool returns information about a given pool, as well as a function to
// create connections on that pool.
class TcpPoolData {
public:
  using OnNewConnectionFn = std::function<void()>;

  TcpPoolData(OnNewConnectionFn on_new_connection, Tcp::ConnectionPool::Instance* pool)
      : on_new_connection_(on_new_connection), pool_(pool) {}

  Envoy::Tcp::ConnectionPool::Cancellable*
  newConnection(Envoy::Tcp::ConnectionPool::Callbacks& callbacks) {
    on_new_connection_();
    return pool_->newConnection(callbacks);
  }

  Upstream::HostDescriptionConstSharedPtr host() const { return pool_->host(); }

private:
  friend class TcpPoolDataPeer;
  OnNewConnectionFn on_new_connection_;
  Tcp::ConnectionPool::Instance* pool_;
};

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
   * @param context the optional load balancer context. Must continue to be
   *        valid until newConnection is called on the pool (if it is to be called).
   * @return the connection pool data or nullopt if there is no host available in the cluster.
   */
  virtual absl::optional<HttpPoolData>
  httpConnPool(ResourcePriority priority, absl::optional<Http::Protocol> downstream_protocol,
               LoadBalancerContext* context) PURE;

  /**
   * Allocate a load balanced TCP connection pool for a cluster. This is *per-thread* so that
   * callers do not need to worry about per thread synchronization. The load balancing policy that
   * is used is the one defined on the cluster when it was created.
   *
   * @param priority the connection pool priority.
   * @param context the optional load balancer context. Must continue to be
   *        valid until newConnection is called on the pool (if it is to be called).
   * @return the connection pool data or nullopt if there is no host available in the cluster.
   */
  virtual absl::optional<TcpPoolData> tcpConnPool(ResourcePriority priority,
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
