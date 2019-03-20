#include "extensions/filters/http/ratelimit/config.h"

#include <chrono>
#include <string>

#include "envoy/config/filter/http/rate_limit/v2/rate_limit.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/common/ratelimit/ratelimit_impl.h"
#include "extensions/filters/http/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

Http::FilterFactoryCb RateLimitFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::rate_limit::v2::RateLimit& proto_config, const std::string&,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.domain().empty());
  FilterConfigSharedPtr filter_config(new FilterConfig(proto_config, context.localInfo(),
                                                       context.scope(), context.runtime(),
                                                       context.httpContext()));
  const std::chrono::milliseconds timeout =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20));

  return [proto_config, &context, timeout,
          filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(
        filter_config, Filters::Common::RateLimit::rateLimitClient(
                           context, proto_config.rate_limit_service().grpc_service(), timeout)));
  };
}

Http::FilterFactoryCb
RateLimitFilterConfig::createFilterFactory(const Json::Object& json_config,
                                           const std::string& stats_prefix,
                                           Server::Configuration::FactoryContext& context) {
  envoy::config::filter::http::rate_limit::v2::RateLimit proto_config;
  Config::FilterJson::translateHttpRateLimitFilter(json_config, proto_config);
  return createFilterFactoryFromProtoTyped(proto_config, stats_prefix, context);
}

/**
 * Static registration for the rate limit filter. @see RegisterFactory.
 */
REGISTER_FACTORY(RateLimitFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
