#include "source/extensions/common/synthetic_ip/cache.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace SyntheticIp {

SyntheticIpCache::SyntheticIpCache(Event::Dispatcher& dispatcher,
                                   std::chrono::seconds eviction_interval, uint32_t max_entries)
    : dispatcher_(dispatcher), eviction_interval_(eviction_interval), max_entries_(max_entries) {

  // Start periodic eviction timer
  eviction_timer_ = dispatcher_.createTimer([this]() {
    evictExpired();
    // Re-arm timer for next eviction
    eviction_timer_->enableTimer(eviction_interval_);
  });

  // Enable timer for first eviction
  eviction_timer_->enableTimer(eviction_interval_);

  ENVOY_LOG(debug, "Synthetic IP cache initialized: eviction_interval={}s, max_entries={}",
            eviction_interval_.count(), max_entries_);
}

SyntheticIpCache::~SyntheticIpCache() {
  if (eviction_timer_) {
    eviction_timer_->disableTimer();
  }
}

void SyntheticIpCache::put(absl::string_view synthetic_ip, absl::string_view hostname,
                           std::chrono::seconds ttl) {
  auto now = dispatcher_.timeSource().monotonicTime();

  CacheEntry entry{
      .hostname = std::string(hostname),
      .expires_at = now + ttl,
      .last_accessed = now,
  };

  // Check if we need to evict oldest entries
  if (cache_.size() >= max_entries_ && cache_.find(synthetic_ip) == cache_.end()) {
    // Cache is full and this is a new entry
    evictOldest();
  }

  cache_[std::string(synthetic_ip)] = std::move(entry);

  ENVOY_LOG(trace, "Cached mapping: {} -> {}, expires in {}s", synthetic_ip, hostname, ttl.count());
}

absl::optional<CacheEntry> SyntheticIpCache::lookup(absl::string_view synthetic_ip) {
  auto it = cache_.find(synthetic_ip);
  if (it == cache_.end()) {
    ENVOY_LOG(trace, "Cache miss: {}", synthetic_ip);
    return absl::nullopt;
  }

  auto now = dispatcher_.timeSource().monotonicTime();

  // Check if expired
  if (it->second.isExpired(now)) {
    ENVOY_LOG(trace, "Cache entry expired: {}", synthetic_ip);
    cache_.erase(it);
    return absl::nullopt;
  }

  // Update last accessed time
  it->second.last_accessed = now;

  ENVOY_LOG(trace, "Cache hit: {} -> {}", synthetic_ip, it->second.hostname);

  return it->second;
}

bool SyntheticIpCache::contains(absl::string_view synthetic_ip) {
  auto it = cache_.find(synthetic_ip);
  if (it == cache_.end()) {
    return false;
  }

  auto now = dispatcher_.timeSource().monotonicTime();
  if (it->second.isExpired(now)) {
    cache_.erase(it);
    return false;
  }

  return true;
}

void SyntheticIpCache::remove(absl::string_view synthetic_ip) {
  auto it = cache_.find(synthetic_ip);
  if (it != cache_.end()) {
    ENVOY_LOG(trace, "Removing cache entry: {}", synthetic_ip);
    cache_.erase(it);
  }
}

void SyntheticIpCache::clear() {
  ENVOY_LOG(debug, "Clearing cache: {} entries", cache_.size());
  cache_.clear();
}

void SyntheticIpCache::evictExpired() {
  auto now = dispatcher_.timeSource().monotonicTime();
  size_t evicted_count = 0;

  for (auto it = cache_.begin(); it != cache_.end();) {
    if (it->second.isExpired(now)) {
      ENVOY_LOG(trace, "Evicting expired entry: {}", it->first);
      cache_.erase(it++);
      evicted_count++;
    } else {
      ++it;
    }
  }

  if (evicted_count > 0) {
    ENVOY_LOG(debug, "Evicted {} expired entries, {} entries remaining", evicted_count,
              cache_.size());
  }
}

void SyntheticIpCache::evictOldest() {
  if (cache_.empty()) {
    return;
  }

  // Find oldest entry by last_accessed time
  auto oldest_it = cache_.begin();
  for (auto it = cache_.begin(); it != cache_.end(); ++it) {
    if (it->second.last_accessed < oldest_it->second.last_accessed) {
      oldest_it = it;
    }
  }

  ENVOY_LOG(debug, "Cache full, evicting oldest entry: {}", oldest_it->first);
  cache_.erase(oldest_it);
}

} // namespace SyntheticIp
} // namespace Common
} // namespace Extensions
} // namespace Envoy
