#pragma once

#include "source/extensions/filters/http/cache/hazelcast_http_cache/config.pb.h"
#include "hazelcast/client/ClientConfig.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

class ConfigUtil {
public:

  static uint64_t validPartitionSize(const uint64_t& config_value) {
    return config_value == 0 ?
      DEFAULT_PARTITION_SIZE : (config_value > MAX_PARTITION_SIZE) ?
      MAX_PARTITION_SIZE : config_value;
  }

  static uint64_t validMaxBodySize(const uint64_t& config_value) {
    return config_value == 0 || (config_value > MAX_BODY_SIZE) ?
      MAX_BODY_SIZE : config_value;
  }

  static hazelcast::client::ClientConfig getClientConfig(const
    envoy::source::extensions::filters::http::cache::HazelcastHttpCacheConfig& cache_config) {
    // TODO: Add retry config, connection config. Use multiple addresses.
    hazelcast::client::ClientConfig config;
    config.getGroupConfig().setName(cache_config.group_name());
    config.getNetworkConfig().addAddress(hazelcast::client::Address(cache_config.ip(),
      cache_config.port()));
    return config;
  }

  static short partitionWarnLimit() {
    return WARN_PARTITION_LIMIT;
  }

private:
  // TODO: Examine the optimal values for defaults and limits.
  static constexpr short WARN_PARTITION_LIMIT = 20;
  static constexpr uint64_t DEFAULT_PARTITION_SIZE = 4096;
  static constexpr uint64_t MAX_PARTITION_SIZE = DEFAULT_PARTITION_SIZE * 16;
  static constexpr uint64_t MAX_BODY_SIZE = MAX_PARTITION_SIZE * 16;
};

} // HazelcastHttpCache
} // Cache
} // HttpFilters
} // Extensions
} // Envoy
