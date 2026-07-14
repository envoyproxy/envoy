#include "source/extensions/filters/http/local_ratelimit/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/local_ratelimit/local_ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

absl::StatusOr<Http::FilterFactoryCb> LocalRateLimitFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {

  absl::Status creation_status = absl::OkStatus();
  FilterConfigSharedPtr filter_config = std::make_shared<FilterConfig>(
      proto_config, context.serverFactoryContext(), context.scope(), creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(filter_config));
  };
}

absl::StatusOr<Http::FilterFactoryCb>
LocalRateLimitFilterConfig::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit& proto_config,
    const std::string&, Server::Configuration::ServerFactoryContext& context) {

  absl::Status creation_status = absl::OkStatus();
  FilterConfigSharedPtr filter_config =
      std::make_shared<FilterConfig>(proto_config, context, context.scope(), creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(filter_config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
LocalRateLimitFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit& proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  absl::Status creation_status = absl::OkStatus();
  auto filter_config =
      std::make_shared<const FilterConfig>(proto_config, context, context.scope(), creation_status,
                                           /*per_route=*/true);
  RETURN_IF_NOT_OK_REF(creation_status);
  return filter_config;
}

/**
 * Static registration for the rate limit filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(LocalRateLimitFilterConfig,
                        Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.local_rate_limit");

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
