#include "source/extensions/filters/http/ratelimit/config.h"

#include <chrono>
#include <string>

#include "envoy/extensions/filters/http/ratelimit/v3/rate_limit.pb.h"
#include "envoy/extensions/filters/http/ratelimit/v3/rate_limit.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/ratelimit/ratelimit_impl.h"
#include "source/extensions/filters/http/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

absl::StatusOr<Http::FilterFactoryCb> RateLimitFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ratelimit::v3::RateLimit& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  auto& server_context = context.serverFactoryContext();

  ASSERT(!proto_config.domain().empty());
  absl::Status status = absl::OkStatus();
  FilterConfigSharedPtr filter_config(new FilterConfig(proto_config, server_context.localInfo(),
                                                       context.scope(), server_context.runtime(),
                                                       server_context, status));
  RETURN_IF_NOT_OK_REF(status);
  // A timeout of 0 means infinite (no timeout). Convert to nullopt in that case.
  const uint64_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20);
  const absl::optional<std::chrono::milliseconds> timeout =
      timeout_ms == 0
          ? absl::nullopt
          : absl::optional<std::chrono::milliseconds>(std::chrono::milliseconds(timeout_ms));

  RETURN_IF_NOT_OK(Config::Utility::checkTransportVersion(proto_config.rate_limit_service()));
  Grpc::GrpcServiceConfigWithHashKey config_with_hash_key =
      Grpc::GrpcServiceConfigWithHashKey(proto_config.rate_limit_service().grpc_service());
  return [config_with_hash_key, &context, timeout,
          filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(
        filter_config,
        Filters::Common::RateLimit::rateLimitClient(context, config_with_hash_key, timeout)));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
RateLimitFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::ratelimit::v3::RateLimitPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  absl::Status status = absl::OkStatus();
  auto route_config = std::make_shared<FilterConfigPerRoute>(context, proto_config, status);
  RETURN_IF_NOT_OK_REF(status);
  return route_config;
}

/**
 * Static registration for the rate limit filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(RateLimitFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.rate_limit");

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
