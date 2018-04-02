#include "extensions/filters/http/ratelimit/config.h"

#include <chrono>
#include <string>

#include "envoy/config/filter/http/rate_limit/v2/rate_limit.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

Server::Configuration::HttpFilterFactoryCb RateLimitFilterConfig::createFilter(
    const envoy::config::filter::http::rate_limit::v2::RateLimit& proto_config, const std::string&,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.domain().empty());
  FilterConfigSharedPtr filter_config(new FilterConfig(proto_config, context.localInfo(),
                                                       context.scope(), context.runtime(),
                                                       context.clusterManager()));
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20);
  return
      [filter_config, timeout_ms, &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(std::make_shared<Filter>(
            filter_config, context.rateLimitClient(std::chrono::milliseconds(timeout_ms))));
      };
}

Server::Configuration::HttpFilterFactoryCb
RateLimitFilterConfig::createFilterFactory(const Json::Object& json_config,
                                           const std::string& stats_prefix,
                                           Server::Configuration::FactoryContext& context) {
  envoy::config::filter::http::rate_limit::v2::RateLimit proto_config;
  Config::FilterJson::translateHttpRateLimitFilter(json_config, proto_config);
  return createFilter(proto_config, stats_prefix, context);
}

Server::Configuration::HttpFilterFactoryCb RateLimitFilterConfig::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<
          const envoy::config::filter::http::rate_limit::v2::RateLimit&>(proto_config),
      stats_prefix, context);
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
