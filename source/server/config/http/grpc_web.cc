#include "server/config/http/grpc_web.h"

#include "common/grpc/grpc_web_filter.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb GrpcWebFilterConfig::createFilterFactory(HttpFilterType type,
                                                             const Json::Object&,
                                                             const std::string&,
                                                             Server::Instance& server) {
  if (type != HttpFilterType::Both) {
    throw EnvoyException(fmt::format(
        "{} gRPC-Web filter must be configured as both a decoder and encoder filter.", name()));
  }

  return [&server](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{new Grpc::GrpcWebFilter()});
  };
}

std::string GrpcWebFilterConfig::name() { return "grpc_web"; }

/**
 * Static registration for the gRPC-Web filter. @see RegisterNamedHttpFilterConfigFactory.
 */
static RegisterNamedHttpFilterConfigFactory<GrpcWebFilterConfig> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
