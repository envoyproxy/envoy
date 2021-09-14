#pragma once

#include "source/common/upstream/conn_pool_map_impl.h"
#include "source/common/upstream/priority_conn_pool_map.h"

namespace Envoy {
namespace Upstream {

template <typename KEY_TYPE, typename POOL_TYPE>
PriorityConnPoolMap<KEY_TYPE, POOL_TYPE>::PriorityConnPoolMap(Envoy::Event::Dispatcher& dispatcher,
                                                              const HostConstSharedPtr& host) {
  for (size_t pool_map_index = 0; pool_map_index < NumResourcePriorities; ++pool_map_index) {
    auto priority = static_cast<ResourcePriority>(pool_map_index);
    conn_pool_maps_[pool_map_index].reset(new ConnPoolMapType(dispatcher, host, priority));
  }
}

template <typename KEY_TYPE, typename POOL_TYPE>
PriorityConnPoolMap<KEY_TYPE, POOL_TYPE>::~PriorityConnPoolMap() = default;

template <typename KEY_TYPE, typename POOL_TYPE>
typename PriorityConnPoolMap<KEY_TYPE, POOL_TYPE>::PoolOptRef
PriorityConnPoolMap<KEY_TYPE, POOL_TYPE>::getPool(ResourcePriority priority, const KEY_TYPE& key,
                                                  const PoolFactory& factory) {
  return conn_pool_maps_[getPriorityIndex(priority)]->getPool(key, factory);
}

template <typename KEY_TYPE, typename POOL_TYPE>
bool PriorityConnPoolMap<KEY_TYPE, POOL_TYPE>::erasePool(ResourcePriority priority,
                                                         const KEY_TYPE& key) {
  return conn_pool_maps_[getPriorityIndex(priority)]->erasePool(key);
}

template <typename KEY_TYPE, typename POOL_TYPE>
size_t PriorityConnPoolMap<KEY_TYPE, POOL_TYPE>::size() const {
  size_t size = 0;
  for (const auto& pool_map : conn_pool_maps_) {
    size += pool_map->size();
  }
  return size;
}

template <typename KEY_TYPE, typename POOL_TYPE>
bool PriorityConnPoolMap<KEY_TYPE, POOL_TYPE>::empty() const {
  for (const auto& pool_map : conn_pool_maps_) {
    if (!pool_map->empty()) {
      return false;
    }
  }
  return true;
}

template <typename KEY_TYPE, typename POOL_TYPE>
void PriorityConnPoolMap<KEY_TYPE, POOL_TYPE>::clear() {
  for (auto& pool_map : conn_pool_maps_) {
    pool_map->clear();
  }
}

template <typename KEY_TYPE, typename POOL_TYPE>
void PriorityConnPoolMap<KEY_TYPE, POOL_TYPE>::addIdleCallback(const IdleCb& cb) {
  for (auto& pool_map : conn_pool_maps_) {
    pool_map->addIdleCallback(cb);
  }
}

template <typename KEY_TYPE, typename POOL_TYPE>
void PriorityConnPoolMap<KEY_TYPE, POOL_TYPE>::drainConnections(
    ConnectionPool::DrainBehavior drain_behavior) {
  for (auto& pool_map : conn_pool_maps_) {
    pool_map->drainConnections(drain_behavior);
  }
}

template <typename KEY_TYPE, typename POOL_TYPE>
size_t PriorityConnPoolMap<KEY_TYPE, POOL_TYPE>::getPriorityIndex(ResourcePriority priority) const {
  size_t index = static_cast<size_t>(priority);
  ASSERT(index < conn_pool_maps_.size());
  return index;
}

} // namespace Upstream
} // namespace Envoy
