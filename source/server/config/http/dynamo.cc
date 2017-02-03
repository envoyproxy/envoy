#include "dynamo.h"

#include "common/dynamo/dynamo_filter.h"

namespace Server {
namespace Configuration {

HttpFilterFactoryCb DynamoFilterConfig::tryCreateFilterFactory(HttpFilterType type,
                                                               const std::string& name,
                                                               const Json::Object&,
                                                               const std::string& stat_prefix,
                                                               Server::Instance& server) {
  if (type != HttpFilterType::Both || name != "http_dynamo_filter") {
    return nullptr;
  }

  return [&server, stat_prefix](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterPtr{
        new Dynamo::DynamoFilter(server.runtime(), stat_prefix, server.stats())});
  };
}

/**
 * Static registration for the http dynamodb filter. @see RegisterHttpFilterConfigFactory.
 */
static RegisterHttpFilterConfigFactory<DynamoFilterConfig> register_;

} // Configuration
} // Server
