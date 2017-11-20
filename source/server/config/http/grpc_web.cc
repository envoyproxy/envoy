#include "server/config/http/grpc_web.h"

#include "envoy/registry/registry.h"

#include "common/grpc/grpc_web_filter.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb GrpcWebFilterConfig::createFilter(const std::string&, FactoryContext& context) {
  return [&context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        Http::StreamFilterSharedPtr{new Grpc::GrpcWebFilter(context.clusterManager())});
  };
}

/**
 * Static registration for the gRPC-Web filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<GrpcWebFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
