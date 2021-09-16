#include "library/common/extensions/filters/http/socket_selection/config.h"

#include "library/common/extensions/filters/http/socket_selection/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SocketSelection {

Http::FilterFactoryCb SocketSelectionFilterFactory::createFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::socket_selection::SocketSelection&,
    const std::string&, Server::Configuration::FactoryContext&) {

  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<SocketSelectionFilter>());
  };
}

/**
 * Static registration for the SocketSelection filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(SocketSelectionFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace SocketSelection
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
