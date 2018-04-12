#include "extensions/filters/http/grpc_http1_bridge/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/http/grpc_http1_bridge/http1_bridge_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1Bridge {

Server::Configuration::HttpFilterFactoryCb
GrpcHttp1BridgeFilterConfig::createFilter(const std::string&,
                                          Server::Configuration::FactoryContext& context) {
  return [&context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Http1BridgeFilter>(context.clusterManager()));
  };
}

/**
 * Static registration for the grpc HTTP1 bridge filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<GrpcHttp1BridgeFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace GrpcHttp1Bridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
