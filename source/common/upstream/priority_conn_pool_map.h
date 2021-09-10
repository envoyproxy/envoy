#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/upstream/resource_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/common/upstream/conn_pool_map.h"

namespace Envoy {
namespace Upstream {
/**
 *  A class mapping keys to connection pools, with some recycling logic built in.
 */
template <typename KEY_TYPE, typename POOL_TYPE> class PriorityConnPoolMap {
public:
  using ConnPoolMapType = ConnPoolMap<KEY_TYPE, POOL_TYPE>;
  using PoolFactory = typename ConnPoolMapType::PoolFactory;
  using IdleCb = typename ConnPoolMapType::IdleCb;
  using PoolOptRef = typename ConnPoolMapType::PoolOptRef;

  PriorityConnPoolMap(Event::Dispatcher& dispatcher, const HostConstSharedPtr& host);
  ~PriorityConnPoolMap();
  /**
   * Returns an existing pool for the given priority and `key`, or creates a new one using
   * `factory`. Note that it is possible for this to fail if a limit on the number of pools allowed
   * is reached.
   * @return The pool corresponding to `key`, or `absl::nullopt`.
   */
  PoolOptRef getPool(ResourcePriority priority, const KEY_TYPE& key, const PoolFactory& factory);

  /**
   * Erase a pool for the given priority and `key` if it exists and is idle.
   */
  bool erasePool(ResourcePriority priority, const KEY_TYPE& key);

  /**
   * @return the number of pools across all priorities.
   */
  size_t size() const;

  /**
   * @return true if the pools across all priorities are empty.
   */
  bool empty() const;

  /**
   * Destroys all mapped pools.
   */
  void clear();

  /**
   * Adds a drain callback to all mapped pools. Any future mapped pools with have the callback
   * automatically added. Be careful with the callback. If it itself calls into `this`, modifying
   * the state of `this`, there is a good chance it will cause corruption due to the callback firing
   * immediately.
   */
  void addIdleCallback(const IdleCb& cb);

  /**
   * See `Envoy::ConnectionPool::Instance::drainConnections()`.
   */
  void drainConnections(Envoy::ConnectionPool::DrainBehavior drain_behavior);

private:
  size_t getPriorityIndex(ResourcePriority priority) const;

  std::array<std::unique_ptr<ConnPoolMapType>, NumResourcePriorities> conn_pool_maps_;
};

} // namespace Upstream
} // namespace Envoy
