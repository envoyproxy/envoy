#pragma once

#include <chrono>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace SyntheticIp {

/**
 * Entry stored in the synthetic IP cache.
 */
struct CacheEntry {
  // Original hostname (e.g., "app.azure.com")
  std::string hostname;

  // When this entry expires
  std::chrono::steady_clock::time_point expires_at;

  // Last time this entry was accessed
  std::chrono::steady_clock::time_point last_accessed;

  // Check if entry has expired
  bool isExpired(std::chrono::steady_clock::time_point now) const { return now > expires_at; }
};

/**
 * Thread-local cache for synthetic IP mappings.
 * Each worker thread has its own instance of this cache.
 */
class SyntheticIpCache : public ThreadLocal::ThreadLocalObject,
                         Logger::Loggable<Logger::Id::connection> {
public:
  SyntheticIpCache(Event::Dispatcher& dispatcher, std::chrono::seconds eviction_interval,
                   uint32_t max_entries);

  ~SyntheticIpCache() override;

  /**
   * Store a mapping in the cache.
   * @param synthetic_ip the synthetic IP address (key)
   * @param hostname the original hostname
   * @param ttl time-to-live for this entry
   */
  void put(absl::string_view synthetic_ip, absl::string_view hostname, std::chrono::seconds ttl);

  /**
   * Lookup a mapping in the cache.
   * @param synthetic_ip the synthetic IP address to look up
   * @return the cache entry if found and not expired, nullopt otherwise
   */
  absl::optional<CacheEntry> lookup(absl::string_view synthetic_ip);

  /**
   * Check if cache contains a non-expired entry.
   * @param synthetic_ip the synthetic IP address
   * @return true if entry exists and not expired
   */
  bool contains(absl::string_view synthetic_ip);

  /**
   * Remove an entry from the cache.
   * @param synthetic_ip the synthetic IP address to remove
   */
  void remove(absl::string_view synthetic_ip);

  /**
   * Clear all entries from the cache.
   */
  void clear();

  /**
   * Get the current number of entries in the cache.
   * @return cache size
   */
  size_t size() const { return cache_.size(); }

private:
  // Periodic eviction of expired entries
  void evictExpired();

  // Evict oldest entries if cache is full
  void evictOldest();

  Event::Dispatcher& dispatcher_;
  absl::flat_hash_map<std::string, CacheEntry> cache_;
  Event::TimerPtr eviction_timer_;
  const std::chrono::seconds eviction_interval_;
  const uint32_t max_entries_;
};

} // namespace SyntheticIp
} // namespace Common
} // namespace Extensions
} // namespace Envoy
