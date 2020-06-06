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
    return config_value == 0
               ? DEFAULT_PARTITION_SIZE
               : (config_value > MAX_ALLOWED_PARTITION_SIZE) ? MAX_ALLOWED_PARTITION_SIZE
                                                             : config_value;
  }

  static uint32_t validMaxBodySize(const uint32_t config_value, const bool unified) {
    if (unified) {
      // Apply size limitation for single entry (unified response) on the map.
      return config_value == 0 || (config_value > MAX_ALLOWED_UNIFIED_BODY_SIZE)
                 ? MAX_ALLOWED_UNIFIED_BODY_SIZE
                 : config_value;
    } else {
      // In divided mode, no upper limit for a body size exists. Instead, the max number of
      // partitions for a response is indirectly set by (max_body_size / partition_size) ratio
      // in the plugin configuration.
      return config_value == 0 ? DEFAULT_MAX_DIVIDED_BODY_SIZE : config_value;
    }
  }

  static hazelcast::client::ClientConfig
  getClientConfig(const envoy::source::extensions::filters::http::cache::HazelcastHttpCacheConfig&
                      cache_config) {
    hazelcast::client::ClientConfig config;
    config.getGroupConfig().setName(cache_config.group_name());
    config.getGroupConfig().setPassword(cache_config.group_password());
    for (auto& address : cache_config.addresses()) {
      config.getNetworkConfig().addAddress(
          hazelcast::client::Address(address.ip(), address.port()));
    }
    config.getNetworkConfig().setConnectionTimeout(cache_config.connection_timeout() == 0
                                                       ? DEFAULT_CONNECTION_TIMEOUT_MS
                                                       : cache_config.connection_timeout());
    config.getNetworkConfig().setConnectionAttemptLimit(
        cache_config.connection_attempt_limit() == 0 ? DEFAULT_CONNECTION_ATTEMPT_LIMIT
                                                     : cache_config.connection_attempt_limit());
    config.getNetworkConfig().setConnectionAttemptPeriod(
        cache_config.connection_attempt_period() == 0 ? DEFAULT_CONNECTION_ATTEMPT_PERIOD_MS
                                                      : cache_config.connection_attempt_period());
    config.getConnectionStrategyConfig().setReconnectMode(
        hazelcast::client::config::ClientConnectionStrategyConfig::ReconnectMode::ASYNC);
    config.setProperty("hazelcast.client.invocation.timeout.seconds",
                       std::to_string(cache_config.invocation_timeout() == 0
                                          ? DEFAULT_INVOCATION_TIMEOUT_SEC
                                          : cache_config.invocation_timeout()));
    return config;
  }

  static uint16_t partitionWarnLimit() { return PARTITION_WARN_LIMIT; }

private:
  friend class ConfigUtilsTest;

  // After this much body partitions stored for a response in DIVIDED mode,
  // a suggestion log will be appeared to increase partition size.
  static constexpr uint16_t PARTITION_WARN_LIMIT = 16;

  // Default size for each divided body entry.
  static constexpr uint64_t DEFAULT_PARTITION_SIZE = 1024 * 16;

  // IMap is not optimized for large value sizes. Hence an upper limit is introduced for
  // each stored body entry.
  // Maximum allowed size for each divided body entry.
  static constexpr uint64_t MAX_ALLOWED_PARTITION_SIZE = 1024 * 32;

  // The limit to keep a single map entry size reasonable.
  // Maximum allowed total body size of a unified response.
  static constexpr uint64_t MAX_ALLOWED_UNIFIED_BODY_SIZE = 1024 * 32;

  // Default maximum body size of a divided response.
  static constexpr uint64_t DEFAULT_MAX_DIVIDED_BODY_SIZE = 1024 * 256;

  // Duration to try to reconnect a cluster if a member does not respond.
  static constexpr uint32_t DEFAULT_CONNECTION_TIMEOUT_MS = 5000;

  // Limit of connection attempts before go offline.
  static constexpr uint32_t DEFAULT_CONNECTION_ATTEMPT_LIMIT = 10;

  // Duration between connection retries.
  static constexpr uint32_t DEFAULT_CONNECTION_ATTEMPT_PERIOD_MS = 5000;

  // Duration for an invocation to be cancelled.
  static constexpr uint32_t DEFAULT_INVOCATION_TIMEOUT_SEC = 8;
};

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
