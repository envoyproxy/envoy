#include "source/extensions/filters/http/grpc_web/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/grpc_web/grpc_web_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcWeb {

Http::FilterFactoryCb GrpcWebFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::grpc_web::v3::GrpcWeb&, const std::string&,
    Server::Configuration::FactoryContext& factory_context) {
  return [&factory_context](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(
        std::make_shared<GrpcWebFilter>(factory_context.serverFactoryContext().grpcContext()));
  };
}

/**
 * Static registration for the gRPC-Web filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(GrpcWebFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.grpc_web");

} // namespace GrpcWeb
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
