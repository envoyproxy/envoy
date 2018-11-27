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
  FilterConfigSharedPtr filter_config(
      new FilterConfig(proto_config, context.localInfo(), context.scope(), context.runtime()));
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20);
  // When we introduce rate limit service config in filters, we should validate here that it matches
  // with bootstrap.
  Filters::Common::RateLimit::ClientPtr ratelimit_client =
      Filters::Common::RateLimit::ClientFactory::rateLimitClientFactory(
          context, Filters::Common::RateLimit::rateLimitConfig(context))
          ->create(std::chrono::milliseconds(timeout_ms), context);
  std::shared_ptr<Filter> filter =
      std::make_shared<Filter>(filter_config, std::move(ratelimit_client));
  // This lambda captures the shared_ptrs created above, thus preserving the
  // reference count. Moreover, keep in mind the capture list determines
  // destruction order.
  return [filter_config, filter,
          ratelimit_client](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(filter);
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
static Registry::RegisterFactory<RateLimitFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
