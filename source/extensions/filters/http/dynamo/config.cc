#include "extensions/filters/http/dynamo/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "extensions/filters/http/dynamo/dynamo_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

Http::FilterFactoryCb
DynamoFilterConfig::createFilter(const std::string& stat_prefix,
                                 Server::Configuration::FactoryContext& context) {
  return [&context, stat_prefix](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{new Dynamo::DynamoFilter(
        context.runtime(), stat_prefix, context.scope(), context.dispatcher().timeSystem())});
  };
}

/**
 * Static registration for the http dynamodb filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<DynamoFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
