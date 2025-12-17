#pragma once

#include <chrono>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/singleton/instance.h"
#include "envoy/singleton/manager.h"
#include "envoy/thread_local/thread_local.h"

#include "source/extensions/common/synthetic_ip/cache.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace SyntheticIp {

/**
 * Manager for thread-local synthetic IP caches.
 * Provides a unified interface for storing and looking up mappings
 * across all worker threads.
 */
class SyntheticIpCacheManager : public Singleton::Instance,
                                Logger::Loggable<Logger::Id::connection> {
public:
  SyntheticIpCacheManager(ThreadLocal::SlotAllocator& tls, Event::Dispatcher& main_dispatcher);

  /**
   * Store a mapping in the current worker's cache.
   * @param synthetic_ip the synthetic IP address (key)
   * @param hostname the original hostname
   */
  void put(absl::string_view synthetic_ip, absl::string_view hostname);

  /**
   * Lookup a mapping in the current worker's cache.
   * @param synthetic_ip the synthetic IP address to look up
   * @return the hostname if found, nullopt otherwise
   */
  absl::optional<std::string> lookup(absl::string_view synthetic_ip);

  /**
   * Replicate a mapping to all worker threads.
   * This should be called from the main thread (e.g., from DNS Gateway filter)
   * to ensure all workers have the mapping.
   *
   * @param synthetic_ip the synthetic IP address (key)
   * @param hostname the original hostname
   */
  void replicateToAllWorkers(absl::string_view synthetic_ip, absl::string_view hostname);

  /**
   * Remove an entry from all worker caches.
   * @param synthetic_ip the synthetic IP address to remove
   */
  void removeFromAllWorkers(absl::string_view synthetic_ip);

  /**
   * Check if current worker's cache contains an entry.
   * @param synthetic_ip the synthetic IP address
   * @return true if entry exists and not expired
   */
  bool contains(absl::string_view synthetic_ip);

  /**
   * Safely replicate from worker thread by posting to main thread first.
   * Can be called from any thread.
   * @param synthetic_ip the synthetic IP address (key)
   * @param hostname the original hostname
   */
  void replicateFromWorker(absl::string_view synthetic_ip, absl::string_view hostname);

private:
  ThreadLocal::TypedSlot<SyntheticIpCache> tls_slot_;
  Event::Dispatcher& main_dispatcher_;
};

using SyntheticIpCacheManagerSharedPtr = std::shared_ptr<SyntheticIpCacheManager>;

} // namespace SyntheticIp
} // namespace Common
} // namespace Extensions
} // namespace Envoy
