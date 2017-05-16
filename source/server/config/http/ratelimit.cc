#include "server/config/http/ratelimit.h"

#include <chrono>
#include <string>

#include "common/http/filter/ratelimit.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb RateLimitFilterConfig::tryCreateFilterFactory(HttpFilterType type,
                                                                  const std::string& name,
                                                                  const Json::Object& config,
                                                                  const std::string&,
                                                                  Server::Instance& server) {
  if (type != HttpFilterType::Decoder || name != "rate_limit") {
    return nullptr;
  }

  Http::RateLimit::FilterConfigSharedPtr filter_config(new Http::RateLimit::FilterConfig(
      config, server.localInfo(), server.stats(), server.runtime(), server.clusterManager()));
  return [filter_config, &server](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr{new Http::RateLimit::Filter(
        filter_config, server.rateLimitClient(std::chrono::milliseconds(20)))});
  };
}

/**
 * Static registration for the rate limit filter. @see RegisterHttpFilterConfigFactory.
 */
static RegisterHttpFilterConfigFactory<RateLimitFilterConfig> register_;

} // Configuration
} // Server
} // Envoy
