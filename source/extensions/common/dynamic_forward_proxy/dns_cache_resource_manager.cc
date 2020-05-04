#include "extensions/common/dynamic_forward_proxy/dns_cache_resource_manager.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

DnsCacheResourceManager::DnsCacheResourceManager(
    DnsCacheCircuitBreakersStats cb_stats, Runtime::Loader& loader, const std::string& config_name,
    const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheCircuitBreakers&
        cb_config) {
  const auto& max_requests = cb_config.threshold().max_pending_requests().value();
  const auto runtime_key = fmt::format("circuit_breaker.dns_cache.{}", config_name);

  pending_requests_ =
      std::make_unique<DnsResource>(max_requests, loader, runtime_key, cb_stats.rq_pending_opening_,
                                    cb_stats.rq_pending_remaining_);
}
} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
