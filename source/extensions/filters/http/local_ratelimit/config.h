#pragma once

#include "envoy/extensions/filters/http/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/extensions/filters/http/local_ratelimit/v3/local_rate_limit.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

/**
 * Config registration for the local rate limit filter. @see NamedHttpFilterConfigFactory.
 */
class LocalRateLimitFilterConfig
    : public Common::FactoryBase<
          envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit> {
public:
  LocalRateLimitFilterConfig() : FactoryBase("envoy.filters.http.local_ratelimit") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit& proto_config,
      Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) override;
};

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
