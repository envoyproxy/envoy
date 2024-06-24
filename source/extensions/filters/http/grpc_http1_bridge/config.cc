#include "source/extensions/filters/http/grpc_http1_bridge/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/grpc_http1_bridge/http1_bridge_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1Bridge {

Http::FilterFactoryCb GrpcHttp1BridgeFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::grpc_http1_bridge::v3::Config& proto_config,
    const std::string&, Server::Configuration::FactoryContext& factory_context) {
  return [&factory_context, proto_config](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(std::make_shared<Http1BridgeFilter>(
        factory_context.serverFactoryContext().grpcContext(), proto_config));
  };
}

/**
 * Static registration for the grpc HTTP1 bridge filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(GrpcHttp1BridgeFilterConfig,
                        Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.grpc_http1_bridge");

} // namespace GrpcHttp1Bridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
