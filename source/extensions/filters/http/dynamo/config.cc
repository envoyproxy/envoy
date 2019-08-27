#include "extensions/filters/http/dynamo/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "extensions/filters/http/dynamo/dynamo_filter.h"
#include "extensions/filters/http/dynamo/dynamo_stats.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

Http::FilterFactoryCb
DynamoFilterConfig::createFilter(const std::string& stat_prefix,
                                 Server::Configuration::FactoryContext& context) {
  auto stats = std::make_shared<DynamoStats>(context.scope(), stat_prefix);
  return [&context, stats](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Dynamo::DynamoFilter>(
        context.runtime(), stats, context.dispatcher().timeSource()));
  };
}

/**
 * Static registration for the http dynamodb filter. @see RegisterFactory.
 */
REGISTER_FACTORY(DynamoFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
