#include "extensions/filters/http/grpc_web/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/http/grpc_web/grpc_web_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcWeb {

Http::FilterFactoryCb
GrpcWebFilterConfig::createFilter(const std::string&,
                                  Server::Configuration::FactoryContext& context) {
  return [&context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<GrpcWebFilter>(context.clusterManager()));
  };
}

/**
 * Static registration for the gRPC-Web filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<GrpcWebFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace GrpcWeb
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
