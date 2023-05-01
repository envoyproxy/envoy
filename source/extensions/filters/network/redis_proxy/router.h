#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

#include "source/extensions/filters/network/redis_proxy/conn_pool.h"
#include "source/extensions/filters/network/redis_proxy/mirror_policy.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

/**
 * An resolved route that wraps an upstream connection pool and list of mirror policies
 */
class Route {
public:
  virtual ~Route() = default;

  virtual ConnPool::InstanceSharedPtr upstream() const PURE;

  virtual const MirrorPolicies& mirrorPolicies() const PURE;
};

using RouteSharedPtr = std::shared_ptr<Route>;

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
  virtual RouteSharedPtr upstreamPool(std::string& key) PURE;

  virtual void setReadFilterCallback(Network::ReadFilterCallbacks* callbacks) PURE;
};

using RouterPtr = std::unique_ptr<Router>;

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
