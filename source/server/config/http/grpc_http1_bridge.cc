#include "server/config/http/grpc_http1_bridge.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/grpc/http1_bridge_filter.h"

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb GrpcHttp1BridgeFilterConfig::createFilterFactory(const Json::Object&,
                                                                     const std::string&,
                                                                     FactoryContext& context) {
  return [&context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        Http::StreamFilterSharedPtr{new Grpc::Http1BridgeFilter(context.clusterManager())});
  };
}

/**
 * Static registration for the grpc HTTP1 bridge filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<GrpcHttp1BridgeFilterConfig, NamedHttpFilterConfigFactory>
    register_;

} // Configuration
} // Server
} // Envoy
