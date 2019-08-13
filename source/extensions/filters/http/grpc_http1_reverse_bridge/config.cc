#include "extensions/filters/http/grpc_http1_reverse_bridge/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/http/grpc_http1_reverse_bridge/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1ReverseBridge {

Http::FilterFactoryCb Config::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::grpc_http1_reverse_bridge::v2alpha1::FilterConfig& config,
    const std::string&, Server::Configuration::FactoryContext&) {
  return [config](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        std::make_unique<Filter>(config.content_type(), config.withhold_grpc_frames()));
  };
}

/**
 * Static registration for the grpc http1 reverse bridge filter. @see RegisterFactory.
 */
REGISTER_FACTORY(Config, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GrpcHttp1ReverseBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
