#include "source/extensions/filters/http/connect_grpc_bridge/config.h"

#include "envoy/extensions/filters/http/connect_grpc_bridge/v3/config.pb.h"
#include "envoy/extensions/filters/http/connect_grpc_bridge/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/connect_grpc_bridge/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectGrpcBridge {

absl::StatusOr<Http::FilterFactoryCb>
ConnectGrpcFilterConfigFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::connect_grpc_bridge::v3::FilterConfig&,
    Server::Configuration::ServerFactoryContext&, Server::Configuration::ExtraFactoryContext&) {
  return [](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(std::make_shared<ConnectGrpcBridgeFilter>());
  };
}

/**
 * Static registration for the Connect RPC stats filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ConnectGrpcFilterConfigFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace ConnectGrpcBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
