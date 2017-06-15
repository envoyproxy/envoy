#include "server/config/http/dynamo.h"

#include <string>

#include "common/dynamo/dynamo_filter.h"

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb DynamoFilterConfig::createFilterFactory(const Json::Object&,
                                                            const std::string& stat_prefix,
                                                            FactoryContext& context) {
  return [&context, stat_prefix](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{
        new Dynamo::DynamoFilter(context.runtime(), stat_prefix, context.scope())});
  };
}

/**
 * Static registration for the http dynamodb filter. @see RegisterNamedHttpFilterConfigFactory.
 */
static RegisterNamedHttpFilterConfigFactory<DynamoFilterConfig> register_;

} // Configuration
} // Server
} // Envoy
