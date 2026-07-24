#pragma once

#include "envoy/extensions/filters/http/ratelimit/v3/rate_limit.pb.h"
#include "envoy/extensions/filters/http/ratelimit/v3/rate_limit.pb.validate.h"

#include "source/extensions/filters/common/ratelimit/ratelimit.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

/**
 * Config registration for the rate limit filter. @see NamedHttpFilterConfigFactory.
 */
class RateLimitFilterConfig
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::ratelimit::v3::RateLimit,
          envoy::extensions::filters::http::ratelimit::v3::RateLimitPerRoute> {
public:
  RateLimitFilterConfig() : ExceptionFreeFactoryBase("envoy.filters.http.ratelimit") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::ratelimit::v3::RateLimit& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::ratelimit::v3::RateLimit& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& context) override;

  // Shared factory creation used by both the downstream (FactoryContext) and route/vhost-level
  // (ServerFactoryContext) paths. The FilterConfig stats are scoped to the given scope.
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactory(
      const envoy::extensions::filters::http::ratelimit::v3::RateLimit& proto_config,
      Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope);

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::ratelimit::v3::RateLimitPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
