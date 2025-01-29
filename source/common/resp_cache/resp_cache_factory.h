#pragma once

// This factory creates a ResponseCache object based on the configuration.

#pragma once

#include <memory>

#include "source/common/protobuf/utility.h"

#include "absl/status/statusor.h"
#include "resp_cache.h"
#include "simple_cache.h"

namespace Envoy {
namespace Common {
namespace RespCache {

enum class CacheType { Simple };

template <typename T>
absl::StatusOr<std::unique_ptr<RespCache<T>>>
createCache(CacheType type, const envoy::config::core::v3::TypedExtensionConfig& config,
            Envoy::TimeSource& time_source) {
  if (type == CacheType::Simple) {
    envoy::common::resp_cache::v3::SimpleCacheConfig simple_cache_config;
    if (!Envoy::MessageUtil::unpackTo(config.typed_config(), simple_cache_config).ok()) {
      return absl::InvalidArgumentError("Invalid config type for SimpleCache");
    }
    auto config_ptr =
        std::make_shared<envoy::common::resp_cache::v3::SimpleCacheConfig>(simple_cache_config);
    return std::make_unique<SimpleCache<T>>(config_ptr, time_source);
  } else {
    return absl::InvalidArgumentError("Unsupported cache type");
  }
}

} // namespace RespCache
} // namespace Common
} // namespace Envoy
