#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/thread_local_cluster.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

/**
 * MySQL route of cluster connection pool by database name.
 */
class Route {
public:
  virtual ~Route() = default;
  /**
   * Return the connection pool manager of this cluster.
   * @return thread local cluster which belong to this route
   */
  virtual Upstream::ThreadLocalCluster* upstream() PURE;
  /**
   * Return the name of this cluster.
   * @return name of route.cluster
   */
  virtual const std::string& name() PURE;
};

using RouteSharedPtr = std::shared_ptr<Route>;
class Router {
public:
  virtual ~Router() = default;
  /**
   * Returns a connection pool that matches a given route. When no match is found, return nullptr.
   * @param db the database name.
   * @return a handle to the connection pool.
   */
  virtual RouteSharedPtr upstreamPool(const std::string& db) PURE;
  /**
   * Returns a connection pool of default cluster.
   * @return a handle to the catch_all connection pool.
   */
  virtual RouteSharedPtr defaultPool() PURE;
};

using RouterSharedPtr = std::shared_ptr<Router>;

class RouteFactory {
public:
  virtual ~RouteFactory() = default;
  virtual RouteSharedPtr create(Upstream::ClusterManager* cm, const std::string& cluster_name) PURE;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
