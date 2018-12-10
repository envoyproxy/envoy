#pragma once

#include <functional>
#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {

namespace Upstream {
class LoadBalancerContext;
}

namespace Http {

namespace ConnectionPool {
class Instance;
}

/**
 * Maps downstream requests to upstream connections.
 *
 * Instances of this class implement concrete methods to map downstream requests to upstream
 * connections. Connection pools are partitioned based on the mapping criteria. A pool is assigned
 * to at most one partition at a time. There is no guarantee that all partitions will have an
 * assigned pool. By allowing the mapper to only map pools to partitions on demand, potentially
 * remapping if necessary, we can ensure that memory use scales with the number of concurrent
 * partitions.
 *
 * The implementation may choose the limit the number of concurrent partitions by returning a
 * failure to assign a pool. The implementation owns all pools it creates.
 */
class ConnectionMapper {
public:
  virtual ~ConnectionMapper() = default;

  using IdleCb = std::function<void()>;

  /**
   * Assigns a request to a partitioned connection pool.
   * @param context The load balancer context for the request to be assigned. Will be used to
   *        choose the partition.
   *
   * @return A pointer to the ConnectionPool to which the request is assigned. nullptr if a
   *         pool was not assigned. Note that ownership is retained by @c this.
   */
  virtual ConnectionPool::Instance* assignPool(const Upstream::LoadBalancerContext& context) PURE;

  /**
   * Registers a callback for when pool has no more work to do. This is used to allow the callback
   * to kickstart operations which were waiting for an idle pool.
   */
  virtual void addIdleCallback(IdleCb callback) PURE;

  /**
   * Drains any allocated connection pools
   */
  virtual void drainPools() PURE;

  /**
   * @return true when no pools owned by the mapper are doing any work
   */
  virtual bool allPoolsIdle() const PURE;
};

using ConnPoolBuilder = std::function<std::unique_ptr<ConnectionPool::Instance>()>;
using ConnectionMapperPtr = std::unique_ptr<ConnectionMapper>;
} // namespace Http
} // namespace Envoy
