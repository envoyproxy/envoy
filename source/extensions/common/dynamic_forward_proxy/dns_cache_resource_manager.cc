#include "extensions/common/dynamic_forward_proxy/dns_cache_resource_manager.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

DnsCacheResourceManager::DnsCacheResourceManager(
    Runtime::Loader& loader,
    const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config) {
  uint32_t max_requests = 1024;

  if (config.has_dns_failure_refresh_rate()) {
    const auto& threshold = config.dns_cache_circuit_breaker().threshold();
    max_requests = threshold.max_pending_requests().value();
  }
  const auto runtime_key = fmt::format("circuit_breaker.dns_cache.{}", config.name());

  pending_requests_ = std::make_unique<DnsResource>(max_requests, loader, runtime_key);
}
} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
