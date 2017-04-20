#include "server/config/http/grpc_http1_bridge.h"

#include <string>

#include "common/grpc/http1_bridge_filter.h"

namespace Server {
namespace Configuration {

HttpFilterFactoryCb GrpcHttp1BridgeFilterConfig::tryCreateFilterFactory(HttpFilterType type,
                                                                        const std::string& name,
                                                                        const Json::Object&,
                                                                        const std::string&,
                                                                        Server::Instance& server) {
  if (type != HttpFilterType::Both || name != "grpc_http1_bridge") {
    return nullptr;
  }

  return [&server](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        Http::StreamFilterSharedPtr{new Grpc::Http1BridgeFilter(server.clusterManager())});
  };
}

/**
 * Static registration for the grpc HTTP1 bridge filter. @see RegisterHttpFilterConfigFactory.
 */
static RegisterHttpFilterConfigFactory<GrpcHttp1BridgeFilterConfig> register_;

} // Configuration
} // Server
