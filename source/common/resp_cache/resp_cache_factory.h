#pragma once

// This factory creates a ResponseCache object based on the configuration.

#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "resp_cache.h"
#include "simple_cache.h"

namespace Envoy {
namespace Common {
namespace RespCache {

enum class CacheType { Simple };

template <typename T>
absl::StatusOr<std::unique_ptr<RespCache<T>>>
createCache(CacheType type, std::size_t max_size, int default_ttl_seconds,
            double eviction_candidate_ratio, double eviction_threshold_ratio,
            Envoy::TimeSource& time_source) {
  switch (type) {
  case CacheType::Simple:
    return std::make_unique<SimpleCache<T>>(max_size, default_ttl_seconds, eviction_candidate_ratio,
                                            eviction_threshold_ratio, time_source);
  default:
    return absl::InvalidArgumentError("Unsupported cache type");
  }
}

} // namespace RespCache
} // namespace Common
} // namespace Envoy
