#include "server/config/http/grpc_web.h"

#include "common/grpc/grpc_web_filter.h"

namespace Server {
namespace Configuration {

HttpFilterFactoryCb GrpcWebFilterConfig::tryCreateFilterFactory(HttpFilterType type,
                                                                const std::string& name,
                                                                const Json::Object&,
                                                                const std::string&,
                                                                Server::Instance& server) {
  if (type != HttpFilterType::Both || name != "grpc_web") {
    return nullptr;
  }

  return [&server](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{new Grpc::GrpcWebFilter()});
  };
}

/**
 * Static registration for the gRpc-Web filter. @see RegisterHttpFilterConfigFactory.
 */
static RegisterHttpFilterConfigFactory<GrpcWebFilterConfig> register_;

} // namespace Configuration
} // namespace Server
