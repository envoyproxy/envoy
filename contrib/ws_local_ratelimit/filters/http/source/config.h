#pragma once

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/ws_local_ratelimit/v3alpha/ws_local_ratelimit.pb.h"
#include "contrib/envoy/extensions/filters/http/ws_local_ratelimit/v3alpha/ws_local_ratelimit.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace WsLocalRateLimitFilter {

/**
 * Config registration for the WebSocket local rate limit filter.
 */
class WsLocalRateLimitFilterFactory
    : public Common::UnifiedFactoryBase<
          envoy::extensions::filters::http::ws_local_ratelimit::v3alpha::WsLocalRateLimit> {
public:
  WsLocalRateLimitFilterFactory() : UnifiedFactoryBase("envoy.filters.http.ws_local_ratelimit") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::ws_local_ratelimit::v3alpha::WsLocalRateLimit&
          proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(WsLocalRateLimitFilterFactory);

} // namespace WsLocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
