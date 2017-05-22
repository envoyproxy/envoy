#include "server/config/http/ratelimit.h"

#include <chrono>
#include <string>

#include "common/http/filter/ratelimit.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb RateLimitFilterConfig::createFilterFactory(HttpFilterType type,
                                                               const Json::Object& config,
                                                               const std::string&,
                                                               Server::Instance& server) {
  if (type != HttpFilterType::Decoder) {
    throw EnvoyException(
        fmt::format("{} http filter must be configured as a decoder filter.", name()));
  }

  Http::RateLimit::FilterConfigSharedPtr filter_config(new Http::RateLimit::FilterConfig(
      config, server.localInfo(), server.stats(), server.runtime(), server.clusterManager()));
  return [filter_config, &server](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr{new Http::RateLimit::Filter(
        filter_config, server.rateLimitClient(std::chrono::milliseconds(20)))});
  };
}

std::string RateLimitFilterConfig::name() { return "rate_limit"; }

/**
 * Static registration for the rate limit filter. @see RegisterNamedHttpFilterConfigFactory.
 */
static RegisterNamedHttpFilterConfigFactory<RateLimitFilterConfig> register_;

} // Configuration
} // Server
} // Envoy
