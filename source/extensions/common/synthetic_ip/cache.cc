#include "source/extensions/common/synthetic_ip/cache.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace SyntheticIp {

void SyntheticIpCache::put(absl::string_view synthetic_ip, absl::string_view hostname) {
  cache_[std::string(synthetic_ip)] = std::string(hostname);
  ENVOY_LOG(trace, "Cached mapping: {} -> {}", synthetic_ip, hostname);
}

absl::optional<std::string> SyntheticIpCache::lookup(absl::string_view synthetic_ip) {
  auto it = cache_.find(synthetic_ip);
  if (it == cache_.end()) {
    ENVOY_LOG(trace, "Cache miss: {}", synthetic_ip);
    return absl::nullopt;
  }

  ENVOY_LOG(trace, "Cache hit: {} -> {}", synthetic_ip, it->second);
  return it->second;
}

bool SyntheticIpCache::contains(absl::string_view synthetic_ip) {
  return cache_.find(synthetic_ip) != cache_.end();
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

} // namespace SyntheticIp
} // namespace Common
} // namespace Extensions
} // namespace Envoy
