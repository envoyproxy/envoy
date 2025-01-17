#pragma once

#include <algorithm> // For std::shuffle
#include <random>    // For std::default_random_engine
#include <vector>

#include "envoy/common/time.h"

#include "source/common/common/thread.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Common {
namespace RespCache {

/**
  A simple cache class with TTL.
  It has a random subset eviction policy. This is memory efficient because it does not need to store
  the order of elements. It restricts stored values to 16-bit unsigned integers, making it
  memory efficient.
 */
class RespCache {
public:
  RespCache(std::size_t max_size, int default_ttl_seconds, double eviction_candidate_ratio,
            double eviction_threshold_ratio, Envoy::TimeSource& time_source)
      : max_cache_size(max_size), default_ttl_seconds(default_ttl_seconds),
        eviction_candidate_ratio(eviction_candidate_ratio),
        eviction_threshold_ratio(eviction_threshold_ratio),
        random_generator(std::random_device{}()), time_source_(time_source) {}

  ~RespCache() {
    for (auto& pair : cache_items_map) {
      free(const_cast<char*>(pair.first));
    }
  }

  // Public getter method for max_cache_size
  std::size_t getMaxCacheSize() const { return max_cache_size; }

  bool Insert(const char* key, uint16_t value, int ttl_seconds = -1) {
    Thread::LockGuard lock{mutex_};
    const char* c_key = strdup(key);
    if (ttl_seconds == -1) {
      ttl_seconds = default_ttl_seconds;
    }

    // Add 5% jitter to TTL
    std::uniform_real_distribution<double> distribution(-0.05, 0.05);
    double jitter_factor = distribution(random_generator);
    auto jittered_ttl = std::chrono::seconds(ttl_seconds) +
                        std::chrono::seconds(static_cast<int>(ttl_seconds * jitter_factor));

    auto expiration_time = time_source_.monotonicTime() + jittered_ttl;
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

  // Eviction algorithm emulates LRU by:
  // 1. Advance iterator on the cache randomly
  // 2. Read 1000 items to decide average TTL
  // 3. Look at some (by default, 1% of the whole cache) items, and delete items whose TTL are older
  // than a fraction (by default, 50%) of the average. Eviction takes two configuration parameters:
  // - Eviction candidate ratio: The fraction of the cache to read for potential eviction
  // - Eviction threshold ratio: Evict items with TTL lower than X% of the average TTL
  void Evict() {
    // TODO: convert duration logging to log
    // auto start_time = std::chrono::high_resolution_clock::now();

    // Step 1: Advance pointer randomly
    std::uniform_int_distribution<std::size_t> distribution(0, (1.0 - eviction_candidate_ratio) *
                                                                   max_cache_size);
    std::size_t advance_steps = distribution(random_generator);

    auto it = cache_items_map.begin();
    for (std::size_t i = 0; i < advance_steps && it != cache_items_map.end(); ++i) {
      ++it;
    }
    auto iterator_save = it;

    // Step 2: Read the next min(1000, num_remove_candidates) items and calculate the sum of TTLs
    auto num_remove_candidates =
        static_cast<std::size_t>(eviction_candidate_ratio * max_cache_size);
    auto current_time = time_source_.monotonicTime();
    std::size_t items_to_read = std::min(static_cast<std::size_t>(1000), num_remove_candidates);
    double sum_ttl = 0.0;
    double min_ttl = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < items_to_read && it != cache_items_map.end(); ++i, ++it) {
      auto item_ttl = std::chrono::duration_cast<std::chrono::milliseconds>(
                          it->second.expiration_time - current_time)
                          .count();
      sum_ttl += item_ttl;
      min_ttl = std::min(min_ttl, static_cast<double>(item_ttl));
    }
    double average_ttl = sum_ttl / items_to_read;

    // Step 3: Evict items with TTL lower than a fraction of the average TTL
    std::size_t removed = 0;
    // Evict items with TTL lower than X% of the average TTL
    auto eviction_threshold = std::chrono::milliseconds(
        static_cast<int>(min_ttl + (average_ttl - min_ttl) * eviction_threshold_ratio));

    // TODO: convert to log - std::cout << "Average TTL: " << average_ttl << " ms, Min TTL: " <<
    // min_ttl << " ms, Eviction threshold: " << eviction_threshold.count() << " ms" << std::endl;

    it = iterator_save;
    for (std::size_t i = 0; i < num_remove_candidates && it != cache_items_map.end();) {
      auto item_ttl = it->second.expiration_time - current_time;
      if (item_ttl.count() < 0 || item_ttl < eviction_threshold) {
        auto it_next = std::next(it);
        free(const_cast<char*>(it->first));
        cache_items_map.erase(it);
        it = it_next;
        removed++;
      } else {
        ++it;
        ++i;
      }
    }

    // auto end_time = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time -
    // start_time).count();
    // TODO: convert to log - std::cout << "Removed " << removed << " objects in " << duration << "
    // microseconds" << std::endl;
    // TODO: convert to log - std::cout << "Resulting cache size: " << cache_items_map.size() <<
    // std::endl;
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
  // Note that this is a single mutex for the entire cache. This makes all Worker threads to
  // synchronize on this mutex. This is not a problem. We implemented a cache which breaks up the
  // mutex and flat_hash_map into 100 buckets. We then tested with 1,000 threads making requests at
  // the same time. The result - the number of buckets (1 or 100) did not make any difference.

  std::size_t max_cache_size;
  int default_ttl_seconds;
  double eviction_candidate_ratio;
  double eviction_threshold_ratio;
  std::default_random_engine random_generator; // Random number generator
  Envoy::TimeSource& time_source_;             // Reference to TimeSource
};

} // namespace RespCache
} // namespace Common
} // namespace Envoy
