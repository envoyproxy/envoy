#include "extensions/filters/http/grpc_http1_reverse_bridge/config.h"

#include "envoy/extensions/filters/http/grpc_http1_reverse_bridge/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_http1_reverse_bridge/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/grpc_http1_reverse_bridge/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1ReverseBridge {

Http::FilterFactoryCb Config::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3::FilterConfig& config,
    const std::string&, Server::Configuration::FactoryContext&) {
  return [config](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        std::make_shared<Filter>(config.content_type(), config.withhold_grpc_frames()));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr Config::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3::FilterConfigPerRoute&
        proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<FilterConfigPerRoute>(proto_config);
}

/**
 * Static registration for the grpc http1 reverse bridge filter. @see RegisterFactory.
 */
REGISTER_FACTORY(Config, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GrpcHttp1ReverseBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
