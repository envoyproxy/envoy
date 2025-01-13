#pragma once

#include <algorithm> // For std::shuffle
#include <random>    // For std::default_random_engine
#include <vector>

#include "envoy/common/time.h"

#include "source/common/common/thread.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

/**
  A simple cache class with TTL.
  It has a random subset eviction policy. This is memory efficient because it does not need to store
  the order of elements. It restricts stored values to 16-bit unsigned integers, making it
  memory efficient.
 */
class FIFOEvictionCache {
public:
  // By default, TTL will be 10 seconds.
  FIFOEvictionCache(std::size_t max_size, int default_ttl_seconds, Envoy::TimeSource& time_source)
      : max_cache_size(max_size), default_ttl_seconds(default_ttl_seconds),
        time_source_(time_source) {}

  ~FIFOEvictionCache() {
    for (auto& pair : cache_items_map) {
      free(const_cast<char*>(pair.first));
    }
  }

  bool Insert(const char* key, uint16_t value, int ttl_seconds = -1) {
    Thread::LockGuard lock{mutex_};
    const char* c_key = strdup(key);
    if (ttl_seconds == -1) {
      ttl_seconds = default_ttl_seconds;
    }
    auto expiration_time = time_source_.monotonicTime() + std::chrono::seconds(ttl_seconds);
    CacheItem item = {value, expiration_time};
    auto it = cache_items_map.find(c_key);
    if (it == cache_items_map.end()) {
      if (cache_items_map.size() >= max_cache_size) {
        Evict();
      }
      cache_items_map[c_key] = item;
    } else {
      cache_items_map[c_key] = item;
    }
    return true;
  }

  bool Erase(const char* key) {
    Thread::LockGuard lock{mutex_};
    auto it = cache_items_map.find(key);
    if (it != cache_items_map.end()) {
      free(const_cast<char*>(it->first));
      cache_items_map.erase(it);
      return true;
    }
    return false;
  }

  absl::optional<uint16_t> Get(const char* key) {
    Thread::LockGuard lock{mutex_};
    auto it = cache_items_map.find(key);
    if (it != cache_items_map.end()) {
      if (time_source_.monotonicTime() < it->second.expiration_time) {
        return it->second.value;
      } else {
        // Item has expired
        free(const_cast<char*>(it->first));
        cache_items_map.erase(it);
      }
    }
    return absl::nullopt;
  }

  size_t Size() const {
    Thread::LockGuard lock{mutex_};
    return cache_items_map.size();
  }

private:
  struct CacheItem {
    uint16_t value;
    std::chrono::steady_clock::time_point expiration_time;
  };

  // Remove first 0.1% of max_cache_size objects.
  // We may want to implement a more sophisticated eviction policy in the future.
  void Evict() {
    size_t items_to_remove = max_cache_size / 1000;
    for (auto it = cache_items_map.begin(); it != cache_items_map.end() && items_to_remove > 0;) {
      auto to_delete = it++;
      free(const_cast<char*>(to_delete->first));
      cache_items_map.erase(to_delete);
      --items_to_remove;
    }
  }
  struct CharPtrHash {
    std::size_t operator()(const char* str) const {
      std::size_t hash = 0;
      while (*str) {
        hash = hash * 101 + *str++;
      }
      return hash;
    }
  };

  struct CharPtrEqual {
    bool operator()(const char* lhs, const char* rhs) const { return std::strcmp(lhs, rhs) == 0; }
  };

  absl::flat_hash_map<const char*, CacheItem, CharPtrHash, CharPtrEqual> cache_items_map;

  mutable Thread::MutexBasicLockable
      mutex_; // Mark mutex_ as mutable to allow locking in const methods

  std::size_t max_cache_size;
  int default_ttl_seconds;
  Envoy::TimeSource& time_source_; // Reference to TimeSource
};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
