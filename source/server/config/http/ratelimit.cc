#include "envoy/server/instance.h"

#include "common/http/filter/ratelimit.h"
#include "server/config/network/http_connection_manager.h"

namespace Server {
namespace Configuration {

/**
 * Config registration for the rate limit filter. @see HttpFilterConfigFactory.
 */
class RateLimitFilterConfig : public HttpFilterConfigFactory {
public:
  HttpFilterFactoryCb tryCreateFilterFactory(HttpFilterType type, const std::string& name,
                                             const Json::Object& config, const std::string&,
                                             Server::Instance& server) override {
    if (type != HttpFilterType::Decoder || name != "rate_limit") {
      return nullptr;
    }

    Http::RateLimitFilterConfigPtr filter_config(new Http::RateLimitFilterConfig(
        config, server.options().serviceClusterName(), server.stats(), server.runtime()));
    return [filter_config, &server](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{new Http::RateLimitFilter(
          filter_config, server.rateLimitClient(std::chrono::milliseconds(20)))});
    };
  }
};

/**
 * Static registration for the rate limit filter. @see RegisterHttpFilterConfigFactory.
 */
static RegisterHttpFilterConfigFactory<RateLimitFilterConfig> register_;

} // Configuration
} // Server
