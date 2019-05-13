#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/filter/network/redis_proxy/v2/redis_proxy.pb.h"

#include "extensions/filters/network/redis_proxy/conn_pool.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

/*
 * Decorator of a connection pool in order to enable key based routing.
 */
class Router {
public:
  virtual ~Router() = default;

  /**
   * Returns a connection pool that matches a given route. When no match is found, the catch all
   * pool is used. When remove prefix is set to true, the prefix will be removed from the key.
   * @param key mutable reference to the key of the current command.
   * @return a handle to the connection pool.
   */
  virtual ConnPool::InstanceSharedPtr upstreamPool(std::string& key) PURE;
};

typedef std::unique_ptr<Router> RouterPtr;

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
