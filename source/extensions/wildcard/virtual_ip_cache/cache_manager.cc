#include "source/extensions/wildcard/virtual_ip_cache/cache_manager.h"

#include <arpa/inet.h>

#include <chrono>

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Wildcard {
namespace VirtualIp {

VirtualIpCacheManager::VirtualIpCacheManager(ThreadLocal::SlotAllocator& tls,
                                             Event::Dispatcher& main_dispatcher,
                                             TimeSource& time_source, const std::string& cidr_base,
                                             uint32_t cidr_prefix_len)
    : tls_slot_(tls), main_dispatcher_(main_dispatcher), time_source_(time_source),
      cidr_base_(cidr_base), cidr_prefix_len_(cidr_prefix_len) {
  // Initialize TLS cache on all workers
  tls_slot_.set([](Event::Dispatcher&) { return std::make_shared<VirtualIpCache>(); });
  ENVOY_LOG(info, "Virtual IP cache manager initialized with CIDR {}/{}", cidr_base_,
            cidr_prefix_len_);
}

absl::optional<std::string> VirtualIpCacheManager::lookup(absl::string_view virtual_ip) {
  if (tls_slot_.get().has_value()) {
    return tls_slot_->lookup(virtual_ip);
  }
  return absl::nullopt;
}

void VirtualIpCacheManager::allocate(const std::string& domain,
                                     Event::Dispatcher& worker_dispatcher,
                                     std::function<void(std::string)> callback) {
  // Post allocation request to main thread
  main_dispatcher_.post([this, domain, &worker_dispatcher, callback]() {
    allocateOnMainThread(domain, worker_dispatcher, callback);
  });
}

// This function should only ever be called within main thread context.
void VirtualIpCacheManager::allocateOnMainThread(const std::string& domain,
                                                 Event::Dispatcher& worker_dispatcher,
                                                 std::function<void(std::string)> callback) {

  // This runs on MAIN THREAD
  ENVOY_LOG(debug, "Main thread allocating IP for domain: {}", domain);

  // Check if we already allocated for this domain
  // No lock needed - this function only runs on main thread event loop (sequential)
  auto domain_it = domain_to_ip_.find(domain);
  if (domain_it != domain_to_ip_.end()) {
    // Already allocated, reuse same IP
    std::string existing_ip = domain_it->second;
    ENVOY_LOG(debug, "Reusing existing allocation: {} -> {}", domain, existing_ip);

    worker_dispatcher.post([callback, existing_ip]() { callback(existing_ip); });
    return;
  }

  // Increment global counter
  uint32_t counter = ++allocation_counter_;

  // Calculate IP offset from global CIDR
  uint32_t max_ips = (1u << (32 - cidr_prefix_len_)) - 2;
  uint32_t ip_offset = ((counter - 1) % max_ips) + 1;

  // Parse base IP
  struct in_addr addr;
  inet_pton(AF_INET, cidr_base_.c_str(), &addr);
  uint32_t base_ip = ntohl(addr.s_addr);
  uint32_t virtual_ip_int = base_ip + ip_offset;

  // Convert to string
  addr.s_addr = htonl(virtual_ip_int);
  char ip_str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &addr, ip_str, INET_ADDRSTRLEN);
  std::string allocated_ip(ip_str);

  ENVOY_LOG(info, "Main thread allocated {} -> {}", domain, allocated_ip);

  // Store in main cache for deduplication
  // No lock needed - main thread event loop is sequential
  domain_to_ip_[domain] = allocated_ip;

  // Capture start time for latency measurement
  auto replication_start = time_source_.monotonicTime();

  // Replicate to all workers, then trigger callback
  tls_slot_.runOnAllThreads(
      // Update callback - runs on each worker
      [allocated_ip, domain](OptRef<VirtualIpCache> cache) {
        if (cache.has_value()) {
          cache->put(allocated_ip, domain);
        }
      },
      // Completion callback - runs on main thread when ALL workers done
      [&worker_dispatcher, callback, allocated_ip, replication_start,
       &time_source = time_source_]() {
        auto replication_end = time_source.monotonicTime();
        auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(replication_end -
                                                                                replication_start)
                              .count();

        ENVOY_LOG(info, "Replication complete for {} in {} microseconds ({}ms)", allocated_ip,
                  latency_us, latency_us / 1000.0);

        // Post callback to original worker
        worker_dispatcher.post([callback, allocated_ip]() { callback(allocated_ip); });
      });

  // Main thread returns immediately (non-blocking!)
  ENVOY_LOG(debug, "Replication started for {}", allocated_ip);
}

std::shared_ptr<VirtualIpCacheManager>
VirtualIpCacheManager::singleton(Server::Configuration::ServerFactoryContext& context) {
  return context.singletonManager().getTyped<VirtualIpCacheManager>(
      "virtual_ip_cache_manager_singleton");
}

} // namespace VirtualIp
} // namespace Wildcard
} // namespace Extensions
} // namespace Envoy
