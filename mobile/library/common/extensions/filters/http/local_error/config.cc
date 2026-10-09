#include "library/common/extensions/filters/http/local_error/config.h"

#include "library/common/extensions/filters/http/local_error/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalError {

absl::StatusOr<Http::FilterFactoryCb>
LocalErrorFilterFactory::createHttpFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::local_error::LocalError&,
    Server::Configuration::ServerFactoryContext&, Server::Configuration::ExtraFactoryContext&) {

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
