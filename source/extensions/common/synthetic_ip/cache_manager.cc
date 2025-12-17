#include "source/extensions/common/synthetic_ip/cache_manager.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace SyntheticIp {

SyntheticIpCacheManager::SyntheticIpCacheManager(ThreadLocal::SlotAllocator& tls,
                                                 Event::Dispatcher& main_dispatcher)
    : tls_slot_(tls), main_dispatcher_(main_dispatcher) {

  // Initialize cache on all workers
  tls_slot_.set([](Event::Dispatcher&) { return std::make_shared<SyntheticIpCache>(); });

  ENVOY_LOG(info, "Synthetic IP cache manager initialized");
}

void SyntheticIpCacheManager::put(absl::string_view synthetic_ip, absl::string_view hostname) {
  // Store in current worker's cache
  if (tls_slot_.get().has_value()) {
    tls_slot_->put(synthetic_ip, hostname);
  }
}

absl::optional<std::string> SyntheticIpCacheManager::lookup(absl::string_view synthetic_ip) {
  // Lookup in current worker's cache
  if (!tls_slot_.get().has_value()) {
    return absl::nullopt;
  }
  return tls_slot_->lookup(synthetic_ip);
}

bool SyntheticIpCacheManager::contains(absl::string_view synthetic_ip) {
  if (!tls_slot_.get().has_value()) {
    return false;
  }
  return tls_slot_->contains(synthetic_ip);
}

void SyntheticIpCacheManager::replicateToAllWorkers(absl::string_view synthetic_ip,
                                                    absl::string_view hostname) {
  // Capture by value for posting to worker threads
  std::string synthetic_ip_copy(synthetic_ip);
  std::string hostname_copy(hostname);

  ENVOY_LOG(debug, "Replicating to all workers: {} -> {}", synthetic_ip, hostname);

  // Post to all worker threads
  tls_slot_.runOnAllThreads([synthetic_ip_copy, hostname_copy](OptRef<SyntheticIpCache> cache) {
    if (cache.has_value()) {
      cache->put(synthetic_ip_copy, hostname_copy);
    }
  });
}

void SyntheticIpCacheManager::removeFromAllWorkers(absl::string_view synthetic_ip) {
  // Capture by value
  std::string synthetic_ip_copy(synthetic_ip);

  ENVOY_LOG(debug, "Removing from all workers: {}", synthetic_ip);

  // Post to all worker threads
  tls_slot_.runOnAllThreads([synthetic_ip_copy](OptRef<SyntheticIpCache> cache) {
    if (cache.has_value()) {
      cache->remove(synthetic_ip_copy);
    }
  });
}

void SyntheticIpCacheManager::replicateFromWorker(absl::string_view synthetic_ip,
                                                  absl::string_view hostname) {
  // Capture by value
  std::string synthetic_ip_copy(synthetic_ip);
  std::string hostname_copy(hostname);

  // Post to main thread, which will then replicate to all workers
  main_dispatcher_.post([this, synthetic_ip_copy, hostname_copy]() {
    replicateToAllWorkers(synthetic_ip_copy, hostname_copy);
  });
}

} // namespace SyntheticIp
} // namespace Common
} // namespace Extensions
} // namespace Envoy

// Register the singleton with the singleton manager
SINGLETON_MANAGER_REGISTRATION(synthetic_ip_cache_manager);
