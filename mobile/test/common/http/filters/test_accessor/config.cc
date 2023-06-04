#include "test/common/http/filters/test_accessor/config.h"

#include "test/common/http/filters/test_accessor/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestAccessor {

Http::FilterFactoryCb TestAccessorFilterFactory::createFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::test_accessor::TestAccessor& proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {

  auto config = std::make_shared<TestAccessorFilterConfig>(proto_config);
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<TestAccessorFilter>(config));
  };
}

/**
 * Static registration for the TestAccessor filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(TestAccessorFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace TestAccessor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
