#pragma once

#include "envoy/common/pure.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace HotKey {
namespace Cache {
namespace LFUCache {
class LFUCache;
} // namespace LFUCache

class Cache {
  friend class LFUCache::LFUCache;

public:
  Cache(const uint8_t& capacity, const uint8_t& warming_capacity = 5)
      : capacity_(capacity), warming_capacity_(warming_capacity) {}
  virtual ~Cache() = default;

  virtual void reset() PURE;
  virtual void touchKey(const std::string& key) PURE;
  virtual void incrKey(const std::string& key, const uint32_t& count) PURE;
  virtual void setKey(const std::string& key, const uint32_t& count) PURE;
  virtual uint8_t getCache(absl::flat_hash_map<std::string, uint32_t>& cache) PURE;
  virtual void attenuate(const uint64_t& attenuate_time_ms = 0) PURE;

private:
  uint8_t capacity_;
  uint8_t warming_capacity_;
};
using CacheSharedPtr = std::shared_ptr<Cache>;

} // namespace Cache
} // namespace HotKey
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
