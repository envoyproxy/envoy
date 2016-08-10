#include "common/router/router.h"
#include "server/config/network/http_connection_manager.h"

namespace Server {
namespace Configuration {

/**
 * Config registration for the router filter. @see HttpFilterConfigFactory.
 */
class FilterConfig : public HttpFilterConfigFactory {
public:
  HttpFilterFactoryCb tryCreateFilterFactory(HttpFilterType type, const std::string& name,
                                             const Json::Object&, const std::string& stat_prefix,
                                             Instance& server) override {
    if (type != HttpFilterType::Decoder || name != "router") {
      return nullptr;
    }

    return [&server, stat_prefix](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{
          new Router::ProdFilter(stat_prefix, server.stats(), server.clusterManager(),
                                 server.runtime(), server.random())});
    };
  }
};

/**
 * Static registration for the router filter. @see RegisterHttpFilterConfigFactory.
 */
static RegisterHttpFilterConfigFactory<FilterConfig> register_;

} // Configuration
} // Server
