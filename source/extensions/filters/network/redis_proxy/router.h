#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/network/redis_proxy/conn_pool.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

/**
 * Per route policy for request mirroring.
 */
class MirrorPolicy {
public:
  virtual ~MirrorPolicy() = default;

  /**
   * @return the upstream connection pool that a matching request should be mirrored to. Returns
   * null if no mirroring should take place.
   */
  virtual ConnPool::InstanceSharedPtr upstream() const PURE;

  /**
   * Determine whether a request should be mirrored.
   * @param command the redis command being requested
   * @return TRUE if mirroring should take place.
   */
  virtual bool shouldMirror(const std::string& command) const PURE;
};

using MirrorPolicyConstSharedPtr = std::shared_ptr<const MirrorPolicy>;

using MirrorPolicies = std::vector<MirrorPolicyConstSharedPtr>;

/**
 * An resolved route that wraps an upstream connection pool and list of mirror policies
 */
class Route {
public:
  virtual ~Route() = default;

  virtual ConnPool::InstanceSharedPtr upstream(const std::string& command) const PURE;

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
   * @param stream_info reference to the stream info used for formatting the key.
   * @return a handle to the connection pool.
   */
  virtual RouteSharedPtr upstreamPool(std::string& key,
                                      const StreamInfo::StreamInfo& stream_info) PURE;
};

using RouterPtr = std::unique_ptr<Router>;

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
