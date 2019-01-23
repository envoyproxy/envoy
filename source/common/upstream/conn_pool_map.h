#pragma once

#include <map>
#include <vector>

#include "envoy/event/dispatcher.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Upstream {
/**
 *  A class mapping keys to connection pools, with some recycling logic built in.
 */
template <typename KEY_TYPE, typename POOL_TYPE> class ConnPoolMap {
public:
  using PoolFactory = std::function<std::unique_ptr<POOL_TYPE>()>;
  using DrainedCb = std::function<void()>;

  ConnPoolMap(Event::Dispatcher& dispatcher);
  ~ConnPoolMap();
  /**
   * Returns an existing pool for `key`, or creates a new one using `factory`. Note that it is
   * possible for this to fail if a limit on the number of pools allowed is reached.
   * @return The pool corresponding to `key`, or `absl::nullopt`
   */
  absl::optional<POOL_TYPE*> getPool(KEY_TYPE key, PoolFactory factory);

  /**
   * @return the number of pools
   */
  size_t size() const;

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
  void addDrainedCallback(DrainedCb cb);

  /**
   * Instructs each connection pool to drain its connections
   */
  void drainConnections();

private:
  std::map<KEY_TYPE, std::unique_ptr<POOL_TYPE>> active_pools_;
  Event::Dispatcher& thread_local_dispatcher_;
  std::vector<DrainedCb> cached_callbacks_;
};

} // namespace Upstream
} // namespace Envoy
