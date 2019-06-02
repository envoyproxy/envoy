#include "extensions/filters/http/grpc_http1_bridge/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/http/grpc_http1_bridge/http1_bridge_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1Bridge {

Http::FilterFactoryCb
GrpcHttp1BridgeFilterConfig::createFilter(const std::string&,
                                          Server::Configuration::FactoryContext& factory_context) {
  if (common_ == nullptr) {
    common_ = std::make_unique<Grpc::Common>(factory_context.scope().symbolTable());
  }
  return [this](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Http1BridgeFilter>(*common_));
  };
}

/**
 * Static registration for the grpc HTTP1 bridge filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GrpcHttp1BridgeFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GrpcHttp1Bridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
