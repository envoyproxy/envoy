#include "extensions/filters/network/redis_proxy/feature/feature.h"

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace Feature {

FeatureConfig::FeatureConfig(
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_FeatureConfig& config,
    Event::Dispatcher& dispatcher, const std::string& prefix, Stats::Scope& scope) {
  std::string feature_prefix(prefix + "feature");
  if (config.has_hotkey()) {
    hk_collector_ = std::make_shared<HotKey::HotKeyCollector>(config.hotkey(), dispatcher,
                                                              feature_prefix, scope);
    hk_collector_->run();
  }
}

} // namespace Feature
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
