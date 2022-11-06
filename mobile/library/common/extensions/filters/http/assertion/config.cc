#include "mobile/library/common/extensions/filters/http/assertion/config.h"

#include "mobile/library/common/extensions/filters/http/assertion/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Assertion {

Http::FilterFactoryCb AssertionFilterFactory::createFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::assertion::Assertion& proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {

  AssertionFilterConfigSharedPtr filter_config =
      std::make_shared<AssertionFilterConfig>(proto_config);
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<AssertionFilter>(filter_config));
  };
}

/**
 * Static registration for the Assertion filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(AssertionFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Assertion
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
