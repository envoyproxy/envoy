#include "test/common/http/filters/assertion/config.h"

#include "test/common/http/filters/assertion/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Assertion {

absl::StatusOr<Http::FilterFactoryCb> AssertionFilterFactory::createHttpFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::assertion::Assertion& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext&) {

  AssertionFilterConfigSharedPtr filter_config =
      std::make_shared<AssertionFilterConfig>(proto_config, context);
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
