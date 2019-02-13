#pragma once

#include "common/upstream/conn_pool_map.h"

namespace Envoy {
namespace Upstream {

template <typename KEY_TYPE, typename POOL_TYPE>
ConnPoolMap<KEY_TYPE, POOL_TYPE>::ConnPoolMap(Envoy::Event::Dispatcher& dispatcher,
                                              absl::optional<uint64_t> max_size)
    : thread_local_dispatcher_(dispatcher), max_size_(max_size) {}

template <typename KEY_TYPE, typename POOL_TYPE>
ConnPoolMap<KEY_TYPE, POOL_TYPE>::~ConnPoolMap() = default;

template <typename KEY_TYPE, typename POOL_TYPE>
typename ConnPoolMap<KEY_TYPE, POOL_TYPE>::OptPoolRef
ConnPoolMap<KEY_TYPE, POOL_TYPE>::getPool(KEY_TYPE key, const PoolFactory& factory) {
  Common::AutoDebugRecursionChecker assert_not_in(recursion_checker_);
  // TODO(klarose): Consider how we will change the connection pool's configuration in the future.
  // The plan is to change the downstream socket options... We may want to take those as a parameter
  // here. Maybe we'll pass them to the factory function?
  auto pool_iter = active_pools_.find(key);
  if (pool_iter != active_pools_.end()) {
    return std::ref(*(pool_iter->second));
  }

  // We need a new pool. Check if we have room.
  if (max_size_.has_value() && size() >= max_size_.value()) {

    // We're full. Try to free up a pool. If we can't, bail out.
    if (!freeOnePool()) {
      // Nothing free. Return failure.
      return absl::nullopt;
    }

    ASSERT(size() < max_size_.value(), "Freeing a pool should reduce the size to below the max.");
    // TODO(klarose): Consider some simple hysteresis here. How can we prevent draining all pools
    // when we only need to free a small percentage? If we start draining once we cross a threshold,
    // then stop after we cross another, we could potentially avoid bouncing pools which shouldn't
    // be freed.
  }

  // We have room for a new pool. Allocate one and let it know about any cached callbacks.
  auto new_pool = factory();
  for (const auto& cb : cached_callbacks_) {
    new_pool->addDrainedCallback(cb);
  }

  auto inserted = active_pools_.emplace(key, std::move(new_pool));
  return std::ref(*inserted.first->second);
}

template <typename KEY_TYPE, typename POOL_TYPE>
size_t ConnPoolMap<KEY_TYPE, POOL_TYPE>::size() const {
  return active_pools_.size();
}

template <typename KEY_TYPE, typename POOL_TYPE> void ConnPoolMap<KEY_TYPE, POOL_TYPE>::clear() {
  Common::AutoDebugRecursionChecker assert_not_in(recursion_checker_);
  for (auto& pool_pair : active_pools_) {
    thread_local_dispatcher_.deferredDelete(std::move(pool_pair.second));
  }

  active_pools_.clear();
}

template <typename KEY_TYPE, typename POOL_TYPE>
void ConnPoolMap<KEY_TYPE, POOL_TYPE>::addDrainedCallback(const DrainedCb& cb) {
  Common::AutoDebugRecursionChecker assert_not_in(recursion_checker_);
  for (auto& pool_pair : active_pools_) {
    pool_pair.second->addDrainedCallback(cb);
  }

  cached_callbacks_.emplace_back(std::move(cb));
}

template <typename KEY_TYPE, typename POOL_TYPE>
void ConnPoolMap<KEY_TYPE, POOL_TYPE>::drainConnections() {
  Common::AutoDebugRecursionChecker assert_not_in(recursion_checker_);
  for (auto& pool_pair : active_pools_) {
    pool_pair.second->drainConnections();
  }
}

template <typename KEY_TYPE, typename POOL_TYPE>
bool ConnPoolMap<KEY_TYPE, POOL_TYPE>::freeOnePool() {
  // Try to find a pool that isn't doing anything.
  auto pool_iter = active_pools_.begin();
  while (pool_iter != active_pools_.end()) {
    if (!pool_iter->second->hasActiveConnections()) {
      break;
    }
    ++pool_iter;
  }

  if (pool_iter != active_pools_.end()) {
    // We found one. Free it up, and let the caller know.
    active_pools_.erase(pool_iter);
    return true;
  }

  return false;
}

} // namespace Upstream
} // namespace Envoy
