#pragma once

#include <string>

#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace SyntheticIp {

/**
 * Thread-local cache for synthetic IP mappings.
 * Each worker thread has its own instance of this cache.
 * Entries are stored indefinitely (no expiration).
 */
class SyntheticIpCache : public ThreadLocal::ThreadLocalObject,
                         Logger::Loggable<Logger::Id::connection> {
public:
  SyntheticIpCache() = default;

  /**
   * Store a mapping in the cache.
   * @param synthetic_ip the synthetic IP address (key)
   * @param hostname the original hostname
   */
  void put(absl::string_view synthetic_ip, absl::string_view hostname);

  /**
   * Lookup a mapping in the cache.
   * @param synthetic_ip the synthetic IP address to look up
   * @return the hostname if found, nullopt otherwise
   */
  absl::optional<std::string> lookup(absl::string_view synthetic_ip);

  /**
   * Check if cache contains an entry.
   * @param synthetic_ip the synthetic IP address
   * @return true if entry exists
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
  absl::flat_hash_map<std::string, std::string> cache_;
};

} // namespace SyntheticIp
} // namespace Common
} // namespace Extensions
} // namespace Envoy
