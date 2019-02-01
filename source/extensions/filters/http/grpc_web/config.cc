#include "extensions/filters/http/grpc_web/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/http/grpc_web/grpc_web_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcWeb {

Http::FilterFactoryCb GrpcWebFilterConfig::createFilter(const std::string&,
                                                        Server::Configuration::FactoryContext&) {
  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<GrpcWebFilter>());
  };
}

/**
 * Static registration for the gRPC-Web filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GrpcWebFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GrpcWeb
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
