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
    // We have hit the maximum number of connection pools. Ask the tracked pools to inform us when
    // we can free them.
    registerFullCallbacks();

    // At this point, all the pools have had drained callbacks registered. Some of them may have
    // immediately invoked the callback. Clean up any that did. If none did, then we cannot allocate
    // a new pool, so we return a failure. Future calls to `getPool` will hopefully be invoked after
    // some pools have drained. Those calls will succeed.
    if (!cleanDrainedPools()) {
      // Nothing free. Return failure.
      return absl::nullopt;
    }

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
void ConnPoolMap<KEY_TYPE, POOL_TYPE>::poolDrained(const KEY_TYPE& key) {
  // If we are in the middle of adding the callbacks when the drain callback comes in, do not free
  // the now idle pool yet. Instead, push its key on to a list which will be cleaned up later once
  // we've finished adding the callbacks. This avoids mutating `active_pools_` while iterating over
  // it.
  if (adding_callbacks_) {
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
  // Here we add drained callbacks to each pool we're tracking so that we can free them up when it
  // is possible. We guard against immediate callbacks by tracking whether we're in the middle of
  // adding them. This is necessary to avoid mutating the `active_pools_` container while we are
  // iterating over it.
  adding_callbacks_ = true;
  for (const auto& pool_pair : active_pools_) {
    KEY_TYPE key = pool_pair.first;
    pool_pair.second->addDrainedCallback([this, key]() { this->poolDrained(key); });
  }
  adding_callbacks_ = false;
}
} // namespace Upstream
} // namespace Envoy
