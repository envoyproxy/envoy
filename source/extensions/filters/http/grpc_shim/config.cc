#include "extensions/filters/http/grpc_shim/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/http/grpc_shim/grpc_shim.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcShim {

Http::FilterFactoryCb GrpcShimConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filter::http::grpc_shim::v2alpha1::GrpcShim& config,
    const std::string&, Server::Configuration::FactoryContext&) {
  return [config](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{new GrpcShim(config.content_type())});
  };
}

/**
 * Static registration for the grpc shim filter. @see RegisterFactory.
 */
static Envoy::Registry::RegisterFactory<GrpcShimConfig,
                                        Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
} // namespace GrpcShim
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
