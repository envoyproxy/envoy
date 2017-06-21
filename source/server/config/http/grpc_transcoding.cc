#include "server/config/http/grpc_transcoding.h"

#include "envoy/registry/registry.h"

#include "common/grpc/transcoding_filter.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb
GrpcTranscodingFilterConfig::createFilterFactory(const Json::Object& config_json,
                                                 const std::string&, FactoryContext&) {
  Grpc::TranscodingConfigSharedPtr config = std::make_shared<Grpc::TranscodingConfig>(config_json);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{new Grpc::TranscodingFilter(*config)});
  };
}

/**
 * Static registration for the grpc transcoding filter. @see RegisterNamedHttpFilterConfigFactory.
 */
static Registry::RegisterFactory<GrpcTranscodingFilterConfig, NamedHttpFilterConfigFactory>
    register_;

} // Configuration
} // Server
} // Envoy
