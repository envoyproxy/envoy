#include "extensions/common/dynamic_forward_proxy/dns_cache_resource_manager.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

DnsCacheResourceManager::DnsCacheResourceManager(
    DnsCacheCircuitBreakersStats&& cb_stats, Runtime::Loader& loader,
    const std::string& config_name,
    const absl::optional<
        envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheCircuitBreakers>& cb_config) {
  if (!cb_config.has_value()) {
    return;
  }
  pending_requests_ = std::make_unique<DnsCacheResourceImpl>(
      cb_config->max_pending_requests().value(), loader,
      fmt::format("dns_cache.{}.circuit_breakers", config_name), cb_stats.rq_pending_opening_,
      cb_stats.rq_pending_remaining_);
}
} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
