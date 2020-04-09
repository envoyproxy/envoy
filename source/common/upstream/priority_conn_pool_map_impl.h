#pragma once

#include "common/upstream/conn_pool_map_impl.h"
#include "common/upstream/priority_conn_pool_map.h"

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
PriorityConnPoolMap<KEY_TYPE, POOL_TYPE>::getPool(ResourcePriority priority, KEY_TYPE key,
                                                  const PoolFactory& factory) {
  size_t index = static_cast<size_t>(priority);
  ASSERT(index < conn_pool_maps_.size());
  return conn_pool_maps_[index]->getPool(key, factory);
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
void PriorityConnPoolMap<KEY_TYPE, POOL_TYPE>::clear() {
  for (auto& pool_map : conn_pool_maps_) {
    pool_map->clear();
  }
}

template <typename KEY_TYPE, typename POOL_TYPE>
void PriorityConnPoolMap<KEY_TYPE, POOL_TYPE>::addDrainedCallback(const DrainedCb& cb) {
  for (auto& pool_map : conn_pool_maps_) {
    pool_map->addDrainedCallback(cb);
  }
}

template <typename KEY_TYPE, typename POOL_TYPE>
void PriorityConnPoolMap<KEY_TYPE, POOL_TYPE>::drainConnections() {
  for (auto& pool_map : conn_pool_maps_) {
    pool_map->drainConnections();
  }
}

} // namespace Upstream
} // namespace Envoy
