#pragma once

#include <algorithm> // For std::shuffle
#include <random>    // For std::default_random_engine
#include <vector>

#include "envoy/common/time.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"

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

  ~RespCache() = default;

  // Public getter method for max_cache_size
  std::size_t getMaxCacheSize() const { return max_cache_size; }

  template <typename T> bool Insert(const std::string& key, const T& value, int ttl_seconds = -1) {
    auto expiration_time = CalculateExpirationTime(ttl_seconds);
    CacheItem item(value, expiration_time);
    return InsertInternal(key, std::move(item));
  }

  bool Erase(const std::string& key) {
    Thread::LockGuard lock{mutex_};
    auto it = cache_items_map.find(key);
    if (it != cache_items_map.end()) {
      cache_items_map.erase(it);
      return true;
    }
    return false;
  }

  template <typename T> absl::optional<T> Get(const std::string& key) {
    Thread::LockGuard lock{mutex_};
    auto it = cache_items_map.find(key);
    if (it != cache_items_map.end()) {
      if (time_source_.monotonicTime() < it->second.expiration_time) {
        return *std::static_pointer_cast<T>(it->second.value);
      } else {
        // Item has expired
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
  // struct to define the cache item stored in the cache.
  // It takes a template so that it can be only status code (to minimize memory footprint),
  // or it can be the full response (to take advantage of full functionality of authz server).
  struct CacheItem {
    std::shared_ptr<void> value;
    std::chrono::steady_clock::time_point expiration_time;

    CacheItem() = default; // default-constructed

    template <typename T>
    CacheItem(const T& val, std::chrono::steady_clock::time_point exp_time)
        : value(std::make_shared<T>(val)), expiration_time(exp_time) {}
  };

  // Calculate expiration time with 5% jitter
  std::chrono::steady_clock::time_point CalculateExpirationTime(int ttl_seconds) {
    if (ttl_seconds == -1) {
      ttl_seconds = default_ttl_seconds;
    }

    std::uniform_real_distribution<double> distribution(-0.05, 0.05);
    double jitter_factor = distribution(random_generator);
    auto jittered_ttl = std::chrono::seconds(ttl_seconds) +
                        std::chrono::seconds(static_cast<int>(ttl_seconds * jitter_factor));

    return time_source_.monotonicTime() + jittered_ttl;
  }

  bool InsertInternal(const std::string& key, CacheItem&& item) {
    Thread::LockGuard lock{mutex_};
    auto it = cache_items_map.find(key);
    if (it == cache_items_map.end()) {
      if (cache_items_map.size() >= max_cache_size) {
        Evict();
      }
      cache_items_map[key] = std::move(item);
    } else {
      cache_items_map[key] = std::move(item);
    }
    return true;
  }

  // Eviction algorithm emulates LRU by:
  // 1. Advance iterator on the cache randomly
  // 2. Read 1000 items to decide average TTL
  // 3. Look at some (by default, 1% of the whole cache) items, and delete items whose TTL are older
  // than a fraction (by default, 50%) of the average. Eviction takes two configuration parameters:
  // - Eviction candidate ratio: The fraction of the cache to read for potential eviction
  // - Eviction threshold ratio: Evict items with TTL lower than X% of the average TTL
  void Evict() {
    // TODO: convert duration logging to log
    // Start measuring real time
    // auto start_real_time = std::chrono::high_resolution_clock::now();
    //  Start measuring CPU time
    // std::clock_t start_cpu_time = std::clock();

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
        cache_items_map.erase(it);
        it = it_next;
        removed++;
      } else {
        ++it;
        ++i;
      }
    }

    // Stop measuring real time
    // auto end_real_time = std::chrono::high_resolution_clock::now();
    // Stop measuring CPU time
    // std::clock_t end_cpu_time = std::clock();

    // Calculate elapsed real time in microseconds
    // auto elapsed_real_time = std::chrono::duration_cast<std::chrono::microseconds>(end_real_time
    // - start_real_time);
    // Calculate elapsed CPU time
    // double elapsed_cpu_time = double(end_cpu_time - start_cpu_time) * 1000000 / CLOCKS_PER_SEC;

    // Output the results
    // std::cout << "Real time: " << elapsed_real_time.count() << " microseconds\n";
    // std::cout << "CPU time: " << elapsed_cpu_time << " microseconds\n";
  }

  absl::flat_hash_map<std::string, CacheItem> cache_items_map;

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
