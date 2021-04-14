#include "extensions/filters/http/composite/config.h"

#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/composite/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

Http::FilterFactoryCb CompositeFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::composite::v3::Composite&,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& factory_context) {

  const auto& prefix = stat_prefix + "composite.";
  auto stats = std::make_shared<FilterStats>(FilterStats{
      ALL_COMPOSITE_FILTER_STATS(POOL_COUNTER_PREFIX(factory_context.scope(), prefix))});

  return [stats](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(*stats));
  };
}

/**
 * Static registration for the composite filter. @see RegisterFactory.
 */
REGISTER_FACTORY(CompositeFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
