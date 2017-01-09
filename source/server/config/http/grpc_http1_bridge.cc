#include "envoy/server/instance.h"

#include "common/grpc/http1_bridge_filter.h"
#include "server/config/network/http_connection_manager.h"

namespace Server {
namespace Configuration {

/**
 * Config registration for the grpc HTTP1 bridge filter. @see HttpFilterConfigFactory.
 */
class GrpcHttp1BridgeFilterConfig : public HttpFilterConfigFactory {
public:
  HttpFilterFactoryCb tryCreateFilterFactory(HttpFilterType type, const std::string& name,
                                             const Json::Object&, const std::string&,
                                             Server::Instance& server) override {
    if (type != HttpFilterType::Both || name != "grpc_http1_bridge") {
      return nullptr;
    }

    return [&server](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(
          Http::StreamFilterPtr{new Grpc::Http1BridgeFilter(server.clusterManager())});
    };
  }
};

/**
 * Static registration for the grpc HTTP1 bridge filter. @see RegisterHttpFilterConfigFactory.
 */
static RegisterHttpFilterConfigFactory<GrpcHttp1BridgeFilterConfig> register_;

} // Configuration
} // Server
