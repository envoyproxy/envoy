#include "test/common/http/filters/test_logger/config.h"

#include "test/common/http/filters/test_logger/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestLogger {

absl::StatusOr<Http::FilterFactoryCb> Factory::createHttpFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::test_logger::TestLogger&,
    Server::Configuration::ServerFactoryContext&, Server::Configuration::ExtraFactoryContext&) {

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
