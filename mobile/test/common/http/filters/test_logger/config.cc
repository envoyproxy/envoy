#include "test/common/http/filters/test_logger/config.h"

#include "test/common/http/filters/test_logger/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestLogger {

Http::FilterFactoryCb Factory::createFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::test_logger::TestLogger&, const std::string&,
    Server::Configuration::FactoryContext&) {

  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>());
  };
}

/**
 * Static registration for the TestEventTracker filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(Factory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace TestLogger
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
