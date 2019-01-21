#pragma once

#include "common/upstream/conn_pool_map.h"

namespace Envoy {
namespace Upstream {

template <typename KEY_TYPE, typename POOL_TYPE>
ConnPoolMap<KEY_TYPE, POOL_TYPE>::ConnPoolMap(Envoy::Event::Dispatcher& dispatcher)
    : thread_local_dispatcher_(dispatcher) {}

template <typename KEY_TYPE, typename POOL_TYPE>
ConnPoolMap<KEY_TYPE, POOL_TYPE>::~ConnPoolMap() = default;

template <typename KEY_TYPE, typename POOL_TYPE>
absl::optional<POOL_TYPE*> ConnPoolMap<KEY_TYPE, POOL_TYPE>::getPool(KEY_TYPE key,
                                                                     PoolFactory factory) {
  // TODO(klarose): Consider how we will change the connection pool's configuration in the future.
  // The plan is to change the downstream socket options... We may want to take those as a parameter
  // here. Maybe we'll pass them to the factory function?
  auto inserted = active_pools_.emplace(key, nullptr);

  // If we inserted a new element, create a pool and assign it to the iterator. Tell it about any
  // cached callbacks.
  if (inserted.second) {
    inserted.first->second = factory();
    for (const auto& cb : cached_callbacks_) {
      inserted.first->second->addDrainedCallback(cb);
    }
  }

  return inserted.first->second.get();
}

template <typename KEY_TYPE, typename POOL_TYPE>
size_t ConnPoolMap<KEY_TYPE, POOL_TYPE>::size() const {
  return active_pools_.size();
}

template <typename KEY_TYPE, typename POOL_TYPE> void ConnPoolMap<KEY_TYPE, POOL_TYPE>::clear() {
  for (auto& pool_pair : active_pools_) {
    thread_local_dispatcher_.deferredDelete(std::move(pool_pair.second));
  }

  active_pools_.clear();
}

template <typename KEY_TYPE, typename POOL_TYPE>
void ConnPoolMap<KEY_TYPE, POOL_TYPE>::addDrainedCallback(DrainedCb cb) {
  for (auto& pool_pair : active_pools_) {
    pool_pair.second->addDrainedCallback(cb);
  }
  cached_callbacks_.emplace_back(std::move(cb));
}

template <typename KEY_TYPE, typename POOL_TYPE>
void ConnPoolMap<KEY_TYPE, POOL_TYPE>::drainConnections() {
  for (auto& pool_pair : active_pools_) {
    pool_pair.second->drainConnections();
  }
}
} // namespace Upstream
} // namespace Envoy
