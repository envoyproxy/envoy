#include "test/common/http/filters/test_read/config.h"

#include "test/common/http/filters/test_read/filter.h"

namespace Envoy {
namespace HttpFilters {
namespace TestRead {

Http::FilterFactoryCb TestReadFilterFactory::createFilterFactoryFromProtoTyped(
    const envoymobile::test::integration::filters::http::test_read::TestRead& /*config*/,
    const std::string&, Server::Configuration::FactoryContext& /*context*/) {
  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<TestReadFilter>());
  };
}

/**
 * Static registration for the TestRead filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(TestReadFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace TestRead
} // namespace HttpFilters
} // namespace Envoy
