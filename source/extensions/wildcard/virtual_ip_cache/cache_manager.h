#pragma once

#include <chrono>
#include <string>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"
#include "envoy/singleton/manager.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Wildcard {
namespace VirtualIp {

/**
 * Thread-local cache for fast IP->hostname lookups.
 */
class VirtualIpCache : public ThreadLocal::ThreadLocalObject {
public:
  void put(absl::string_view virtual_ip, absl::string_view hostname) {
    cache_[std::string(virtual_ip)] = std::string(hostname);
  }

  absl::optional<std::string> lookup(absl::string_view virtual_ip) {
    auto it = cache_.find(virtual_ip);
    return it != cache_.end() ? absl::optional<std::string>(it->second) : absl::nullopt;
  }

private:
  absl::flat_hash_map<std::string, std::string> cache_;
};

/**
 * Virtual IP Cache Manager - coordinates IP allocation and caching.
 * All IP allocation happens on the main thread using a global CIDR block.
 * Replication waits for all workers before triggering callback (non-blocking on main).
 * Lookups are pure TLS (lock-free) - guaranteed to succeed after replication.
 */
class VirtualIpCacheManager : public Singleton::Instance, Logger::Loggable<Logger::Id::connection> {
public:
  VirtualIpCacheManager(ThreadLocal::SlotAllocator& tls, Event::Dispatcher& main_dispatcher,
                        TimeSource& time_source, const std::string& cidr_base,
                        uint32_t cidr_prefix_len);

  /**
   * Lookup hostname for a virtual IP.
   * Checks worker's TLS cache (lock-free, ~50ns).
   * @param virtual_ip the virtual IP address to look up
   * @return the hostname if found, nullopt otherwise
   */
  absl::optional<std::string> lookup(absl::string_view virtual_ip);

  /**
   * Allocate a virtual IP for a domain (async, main thread does allocation).
   * Uses the global CIDR block configured at construction.
   * Callback is invoked on the same worker thread after replication completes.
   *
   * @param domain the domain name to allocate for
   * @param worker_dispatcher the worker thread's dispatcher (for callback)
   * @param callback called with the allocated IP when ready
   */
  void allocate(const std::string& domain, Event::Dispatcher& worker_dispatcher,
                std::function<void(std::string)> callback);

  /**
   * Get the singleton VirtualIpCacheManager instance.
   * Must be created in bootstrap section first.
   */
  static std::shared_ptr<VirtualIpCacheManager>
  singleton(Server::Configuration::ServerFactoryContext& context);

private:
  ThreadLocal::TypedSlot<VirtualIpCache> tls_slot_;
  Event::Dispatcher& main_dispatcher_;
  TimeSource& time_source_;

  // Global CIDR configuration
  const std::string cidr_base_;    // e.g., "100.80.0.0"
  const uint32_t cidr_prefix_len_; // e.g., 16

  // Main thread state (for deduplication and allocation)
  // No mutex needed - allocateOnMainThread() only runs on main thread event loop (sequential)
  uint32_t allocation_counter_ = 0;                            // Single global counter
  absl::flat_hash_map<std::string, std::string> domain_to_ip_; // Domain deduplication cache

  void allocateOnMainThread(const std::string& domain, Event::Dispatcher& worker_dispatcher,
                            std::function<void(std::string)> callback);
};

using VirtualIpCacheManagerSharedPtr = std::shared_ptr<VirtualIpCacheManager>;

} // namespace VirtualIp
} // namespace Wildcard
} // namespace Extensions
} // namespace Envoy
