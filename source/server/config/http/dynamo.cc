#include "server/config/http/dynamo.h"

#include <string>

#include "common/dynamo/dynamo_filter.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb DynamoFilterConfig::createFilterFactory(HttpFilterType type,
                                                            const Json::Object&,
                                                            const std::string& stat_prefix,
                                                            Server::Instance& server) {
  if (type != HttpFilterType::Both) {
    throw EnvoyException(fmt::format(
        "{} http filter must be configured as both a decoder and encoder filter.", name()));
  }

  return [&server, stat_prefix](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{
        new Dynamo::DynamoFilter(server.runtime(), stat_prefix, server.stats())});
  };
}

std::string DynamoFilterConfig::name() { return "http_dynamo_filter"; }

/**
 * Static registration for the http dynamodb filter. @see RegisterNamedHttpFilterConfigFactory.
 */
static RegisterNamedHttpFilterConfigFactory<DynamoFilterConfig> register_;

} // Configuration
} // Server
} // Envoy
