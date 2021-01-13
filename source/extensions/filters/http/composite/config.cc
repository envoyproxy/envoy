#include "extensions/filters/http/composite/config.h"

#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/composite/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

Http::FilterFactoryCb CompositeFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::composite::v3::Composite& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  FactoryMap factory_map;
  for (const auto& type_url : config.type_url()) {
    auto* factory = Registry::FactoryRegistry<
        Server::Configuration::NamedHttpFilterConfigFactory>::getFactoryByType(type_url);
    if (factory == nullptr) {
      throw EnvoyException("no factory found for HTTP filter from type " + type_url);
    }

    factory_map.emplace(type_url, factory);
  }

  return [factory_map, stats_prefix, context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(factory_map, stats_prefix, context));
  };
}

/**
 * Static registration for the composite filter. @see RegisterFactory.
 */
REGISTER_FACTORY(CompositeFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
