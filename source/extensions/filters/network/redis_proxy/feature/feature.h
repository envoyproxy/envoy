#pragma once

#include "extensions/filters/network/redis_proxy/feature/hotkey/hotkey_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace Feature {

/**
 * Configuration for the redis proxy filter feature.
 */
class FeatureConfig {
public:
  FeatureConfig(
      const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_FeatureConfig& config,
      Event::Dispatcher& dispatcher, const std::string& prefix, Stats::Scope& scope);
  ~FeatureConfig() = default;

  HotKey::HotKeyCollectorSharedPtr hotkeyCollector() const { return hk_collector_; }

private:
  HotKey::HotKeyCollectorSharedPtr hk_collector_;
};
using FeatureConfigSharedPtr = std::shared_ptr<FeatureConfig>;

} // namespace Feature
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
