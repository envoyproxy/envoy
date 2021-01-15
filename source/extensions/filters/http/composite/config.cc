#include "extensions/filters/http/composite/config.h"

#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/composite/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

Http::FilterFactoryCb CompositeFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::composite::v3::Composite&, const std::string&,
    Server::Configuration::FactoryContext&) {

  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>());
  };
}

/**
 * Static registration for the composite filter. @see RegisterFactory.
 */
REGISTER_FACTORY(CompositeFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

/**
 * Static registration for the composite action. @see RegisterFactory.
 */
REGISTER_FACTORY(CompositeMatchActionFactory, Matcher::ActionFactory);

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
