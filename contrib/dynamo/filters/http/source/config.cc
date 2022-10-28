#include "contrib/dynamo/filters/http/source/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "contrib/dynamo/filters/http/source/dynamo_filter.h"
#include "contrib/dynamo/filters/http/source/dynamo_stats.h"
#include "contrib/envoy/extensions/filters/http/dynamo/v3/dynamo.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

Http::FilterFactoryCb DynamoFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::dynamo::v3::Dynamo&, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  auto stats = std::make_shared<DynamoStats>(context.scope(), stats_prefix);
  return [&context, stats](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Dynamo::DynamoFilter>(
        context.runtime(), stats, context.mainThreadDispatcher().timeSource()));
  };
}

/**
 * Static registration for the http dynamodb filter. @see RegisterFactory.
 */
REGISTER_FACTORY(DynamoFilterConfig,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.http_dynamo_filter"};

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
