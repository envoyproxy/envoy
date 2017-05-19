#include "server/config/http/grpc_http1_bridge.h"

#include <string>

#include "common/grpc/http1_bridge_filter.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb GrpcHttp1BridgeFilterConfig::createFilterFactory(HttpFilterType type,
                                                                     const Json::Object&,
                                                                     const std::string&,
                                                                     Server::Instance& server) {
  if (type != HttpFilterType::Both) {
    throw EnvoyException(fmt::format(
        "{} http filter must be configured as both a decoder and encoder filter.", name()));
  }

  return [&server](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        Http::StreamFilterSharedPtr{new Grpc::Http1BridgeFilter(server.clusterManager())});
  };
}

std::string GrpcHttp1BridgeFilterConfig::name() { return "grpc_http1_bridge"; }

/**
 * Static registration for the grpc HTTP1 bridge filter. @see RegisterHttpFilterConfigFactory.
 */
static RegisterHttpFilterConfigFactory<GrpcHttp1BridgeFilterConfig> register_;

} // Configuration
} // Server
} // Envoy
