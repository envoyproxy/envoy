#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/upstream/cluster_manager.h"

#include "extensions/filters/network/mysql_proxy/new_conn_pool_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class Route {
public:
  virtual ~Route() = default;
  // cluster connection pool of this route
  virtual ConnPool::ConnectionPoolManager& upstream() PURE;
};

using RouteSharedPtr = std::shared_ptr<Route>;
class Router {
public:
  virtual ~Router() = default;
  /**
   * Returns a connection pool that matches a given route. When no match is found, return nullptr.
   * @param db the db name of current connection.
   * @return a handle to the connection pool.
   */
  virtual RouteSharedPtr upstreamPool(const std::string& db) PURE;
};

using RouterSharedPtr = std::shared_ptr<Router>;

class RouteFactory {
public:
  virtual ~RouteFactory() = default;
  virtual RouteSharedPtr
  create(Upstream::ClusterManager* cm, ThreadLocal::SlotAllocator& tls, Api::Api& api,
         const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::Route& route,
         DecoderFactory& decoder_factory, ConnPool::ConnectionPoolManagerFactory& factory) PURE;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
