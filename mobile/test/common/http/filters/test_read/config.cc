#include "test/common/http/filters/test_read/config.h"

#include "test/common/http/filters/test_read/filter.h"

namespace Envoy {
namespace HttpFilters {
namespace TestRead {

absl::StatusOr<Http::FilterFactoryCb> TestReadFilterFactory::createHttpFilterFactoryFromProtoTyped(
    const envoymobile::test::integration::filters::http::test_read::TestRead& /*config*/,
    Server::Configuration::ServerFactoryContext& /*context*/,
    Server::Configuration::ExtraFactoryContext&) {
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
