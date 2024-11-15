#pragma once

#include <functional>
#include <vector>

#include "envoy/common/conn_pool.h"
#include "envoy/event/dispatcher.h"
#include "envoy/upstream/resource_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/debug_recursion_checker.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Upstream {
/**
 *  A class mapping keys to connection pools, with some recycling logic built in.
 */
template <typename KEY_TYPE, typename POOL_TYPE> class ConnPoolMap {
public:
  using PoolFactory = std::function<std::unique_ptr<POOL_TYPE>()>;
  using IdleCb = typename POOL_TYPE::IdleCb;
  using PoolOptRef = absl::optional<std::reference_wrapper<POOL_TYPE>>;

  ConnPoolMap(Event::Dispatcher& dispatcher, const HostConstSharedPtr& host,
              ResourcePriority priority);
  ~ConnPoolMap();
  /**
   * Returns an existing pool for `key`, or creates a new one using `factory`. Note that it is
   * possible for this to fail if a limit on the number of pools allowed is reached.
   * @return The pool corresponding to `key`, or `absl::nullopt`.
   */
  PoolOptRef getPool(const KEY_TYPE& key, const PoolFactory& factory);

  /**
   * Erases an existing pool mapped to `key`.
   *
   * @return true if the entry exists and was removed, false otherwise
   */
  bool erasePool(const KEY_TYPE& key);

  /**
   * @return the number of pools.
   */
  size_t size() const;

  /**
   * @return true if the pools are empty.
   */
  bool empty() const;

  /**
   * Destroys all mapped pools.
   */
  void clear();

  /**
   * Adds an idle callback to all mapped pools. Any future mapped pools with have the callback
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
  /**
   * Frees the first idle pool in `active_pools_`.
   * @return false if no pool was freed.
   */
  bool freeOnePool();

  /**
   * Cleans up the active_pools_ map and updates resource tracking
   **/
  void clearActivePools();

  absl::flat_hash_map<KEY_TYPE, std::unique_ptr<POOL_TYPE>> active_pools_;
  Event::Dispatcher& thread_local_dispatcher_;
  std::vector<IdleCb> cached_callbacks_;
  const HostConstSharedPtr host_;
  // Keep smaller fields near the end to reduce padding
  const ResourcePriority priority_;
  Common::DebugRecursionChecker recursion_checker_;
};

} // namespace Upstream
} // namespace Envoy
