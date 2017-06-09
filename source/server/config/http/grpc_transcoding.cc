#include "server/config/http/grpc_transcoding.h"

#include "common/grpc/transcoding_filter.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb GrpcTranscodingFilterConfig::createFilterFactory(HttpFilterType type,
                                                                     const Json::Object& config_json,
                                                                     const std::string&,
                                                                     Server::Instance& server) {
  if (type != HttpFilterType::Both) {
    throw EnvoyException(fmt::format(
        "{} http filter must be configured as both a decoder and encoder filter.", name()));
  }

  Grpc::TranscodingConfigSharedPtr config = std::make_shared<Grpc::TranscodingConfig>(config_json);

  return [&server, config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        Http::StreamFilterSharedPtr{new Grpc::TranscodingFilter(*config)});
  };
}

std::string GrpcTranscodingFilterConfig::name() { return "grpc_http1_bridge"; }

/**
 * Static registration for the grpc transcoding filter. @see RegisterNamedHttpFilterConfigFactory.
 */
static RegisterNamedHttpFilterConfigFactory<GrpcTranscodingFilterConfig> register_;

} // Configuration
} // Server
} // Envoy
