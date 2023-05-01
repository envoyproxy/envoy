#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

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

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
