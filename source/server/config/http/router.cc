#include "common/router/router.h"
#include "common/router/shadow_writer_impl.h"
#include "server/config/network/http_connection_manager.h"

namespace Server {
namespace Configuration {

/**
 * Config registration for the router filter. @see HttpFilterConfigFactory.
 */
class FilterConfig : public HttpFilterConfigFactory {
public:
  HttpFilterFactoryCb tryCreateFilterFactory(HttpFilterType type, const std::string& name,
                                             const Json::Object& json_config,
                                             const std::string& stat_prefix,
                                             Instance& server) override {
    if (type != HttpFilterType::Decoder || name != "router") {
      return nullptr;
    }

    Router::FilterConfigPtr config(new Router::FilterConfig(
        stat_prefix, server.options().serviceZone(), server.stats(), server.clusterManager(),
        server.runtime(), server.random(),
        Router::ShadowWriterPtr{new Router::ShadowWriterImpl(server.clusterManager())},
        json_config.getBoolean("dynamic_stats", true)));

    return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(std::make_shared<Router::ProdFilter>(*config));
    };
  }
};

/**
 * Static registration for the router filter. @see RegisterHttpFilterConfigFactory.
 */
static RegisterHttpFilterConfigFactory<FilterConfig> register_;

} // Configuration
} // Server
