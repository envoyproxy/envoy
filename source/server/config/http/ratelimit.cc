#include "server/config/http/ratelimit.h"

#include <chrono>
#include <string>

#include "envoy/registry/registry.h"

#include "common/http/filter/ratelimit.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb RateLimitFilterConfig::createFilterFactory(const Json::Object& config,
                                                               const std::string&,
                                                               FactoryContext& context) {
  Http::RateLimit::FilterConfigSharedPtr filter_config(new Http::RateLimit::FilterConfig(
      config, context.localInfo(), context.scope(), context.runtime(), context.clusterManager()));
  const uint32_t timeout_ms = config.getInteger("timeout_ms", 20);
  return [filter_config, timeout_ms,
          &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr{new Http::RateLimit::Filter(
        filter_config, context.rateLimitClient(std::chrono::milliseconds(timeout_ms)))});
  };
}

/**
 * Static registration for the rate limit filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RateLimitFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
