#include "source/extensions/common/dynamic_forward_proxy/dns_cache_resource_manager.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

DnsCacheResourceManagerImpl::DnsCacheResourceManagerImpl(
    Stats::Scope& scope, Runtime::Loader& loader, const std::string& config_name,
    const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheCircuitBreakers& cb_config)
    : cb_stats_(generateDnsCacheCircuitBreakersStats(scope)),
      pending_requests_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(cb_config, max_pending_requests, 1024),
                        loader, fmt::format("dns_cache.{}.circuit_breakers", config_name),
                        cb_stats_.rq_pending_open_, cb_stats_.rq_pending_remaining_) {}

DnsCacheCircuitBreakersStats
DnsCacheResourceManagerImpl::generateDnsCacheCircuitBreakersStats(Stats::Scope& scope) {
  std::string stat_prefix = "circuit_breakers";
  return {ALL_DNS_CACHE_CIRCUIT_BREAKERS_STATS(POOL_GAUGE_PREFIX(scope, stat_prefix),
                                               POOL_GAUGE_PREFIX(scope, stat_prefix))};
}

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
