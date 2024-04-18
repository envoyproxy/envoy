#include "library/common/extensions/filters/http/local_error/config.h"

#include "library/common/extensions/filters/http/local_error/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalError {

Http::FilterFactoryCb LocalErrorFilterFactory::createFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::local_error::LocalError&, const std::string&,
    Server::Configuration::FactoryContext&) {

  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<LocalErrorFilter>());
  };
}

/**
 * Static registration for the LocalError filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(LocalErrorFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace LocalError
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
