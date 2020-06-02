#include "extensions/common/dynamic_forward_proxy/dns_cache_resource_manager.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

DnsCacheResourceManagerImpl::DnsCacheResourceManagerImpl(
    DnsCacheCircuitBreakersStats&& cb_stats, Runtime::Loader& loader,
    const std::string& config_name,
    const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheCircuitBreakers& cb_config)
    : pending_requests_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(cb_config, max_pending_requests, 1024),
                        loader, fmt::format("dns_cache.{}.circuit_breakers", config_name),
                        cb_stats.rq_pending_open_, cb_stats.rq_pending_remaining_) {}
} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
