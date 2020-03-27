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

  static uint64_t validPartitionSize(const uint64_t config_value) {
    return config_value == 0 ?
      DEFAULT_PARTITION_SIZE : (config_value > MAX_PARTITION_SIZE) ?
      MAX_PARTITION_SIZE : config_value;
  }

  static uint64_t validMaxBodySize(const uint64_t config_value, const bool unified) {
    uint64_t max_size = unified ? MAX_UNIFIED_BODY_SIZE : MAX_DIVIDED_BODY_SIZE;
    return config_value == 0 || (config_value > max_size) ? max_size : config_value;
  }

  static hazelcast::client::ClientConfig getClientConfig(const
    envoy::source::extensions::filters::http::cache::HazelcastHttpCacheConfig& cache_config) {
    hazelcast::client::ClientConfig config;
    config.getGroupConfig().setName(cache_config.group_name());
    config.getNetworkConfig().setConnectionAttemptPeriod(5000).setConnectionAttemptLimit(5);
    config.getConnectionStrategyConfig().setReconnectMode(
        hazelcast::client::config::ClientConnectionStrategyConfig::ReconnectMode::ASYNC);
    for (auto &address : cache_config.addresses()) {
      config.getNetworkConfig().addAddress(hazelcast::client::Address(address.ip(),
          address.port()));
    }
    return config;
  }

  static short partitionWarnLimit() {
    return WARN_PARTITION_LIMIT;
  }

private:
  // After this much body partitions stored for a response in DIVIDED mode,
  // a suggestion log will be appeared to increase partition size.
  static constexpr short WARN_PARTITION_LIMIT = 16;

  // Sizes for each divided body entry.
  static constexpr uint64_t DEFAULT_PARTITION_SIZE = 2048;
  static constexpr uint64_t MAX_PARTITION_SIZE = DEFAULT_PARTITION_SIZE * 32;

  // Size for total body size of a unified response
  static constexpr uint64_t MAX_UNIFIED_BODY_SIZE = MAX_PARTITION_SIZE;

  // Size for total body size of a divided response (at most 32 partitions allowed)
  static constexpr uint64_t MAX_DIVIDED_BODY_SIZE = MAX_UNIFIED_BODY_SIZE * 32;
};

} // HazelcastHttpCache
} // Cache
} // HttpFilters
} // Extensions
} // Envoy
