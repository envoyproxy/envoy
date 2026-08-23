#include "contrib/ws_local_ratelimit/filters/http/source/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "contrib/ws_local_ratelimit/filters/http/source/ws_local_ratelimit_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace WsLocalRateLimitFilter {

absl::StatusOr<Http::FilterFactoryCb>
WsLocalRateLimitFilterFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ws_local_ratelimit::v3alpha::WsLocalRateLimit&
        proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {
  WsLocalRateLimitConfigSharedPtr filter_config(
      new WsLocalRateLimitConfig(proto_config, extra_context.scopeOr(context)));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<WsLocalRateLimitFilter>(filter_config));
  };
}

/**
 * Static registration for the WebSocket local rate limit filter. @see RegisterFactory.
 */
REGISTER_FACTORY(WsLocalRateLimitFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace WsLocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
