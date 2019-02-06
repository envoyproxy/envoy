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

  // We need a new element. If we can, create a pool and add it to the map. Tell it about any cached
  // callbacks.
  if (max_size_.has_value() && size() >= max_size_) {
    registerFullCallbacks();
    if (!cleanDrainedPools()) {
      // nothing free. :( Return failure.
      return absl::nullopt;
    }
  }

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
void ConnPoolMap<KEY_TYPE, POOL_TYPE>::poolDrained(const KEY_TYPE& key) {
  if (adding_pool_) {
    drained_pools_.push_back(key);
  } else {
    active_pools_.erase(key);
  }
}

template <typename KEY_TYPE, typename POOL_TYPE>
bool ConnPoolMap<KEY_TYPE, POOL_TYPE>::cleanDrainedPools() {
  if (drained_pools_.empty()) {
    return false;
  }

  for (auto& key : drained_pools_) {
    active_pools_.erase(key);
  }

  drained_pools_.clear();
  return true;
}

template <typename KEY_TYPE, typename POOL_TYPE>
void ConnPoolMap<KEY_TYPE, POOL_TYPE>::registerFullCallbacks() {
  adding_pool_ = true;
  for (const auto& pool_pair : active_pools_) {
    KEY_TYPE key = pool_pair.first;
    pool_pair.second->addDrainedCallback([this, key]() { this->poolDrained(key); });
  }
  adding_pool_ = false;
}
} // namespace Upstream
} // namespace Envoy
