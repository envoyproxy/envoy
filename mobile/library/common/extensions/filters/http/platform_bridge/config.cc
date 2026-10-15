#include "library/common/extensions/filters/http/platform_bridge/config.h"

#include "library/common/extensions/filters/http/platform_bridge/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PlatformBridge {

absl::StatusOr<Http::FilterFactoryCb>
PlatformBridgeFilterFactory::createHttpFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::platform_bridge::PlatformBridge& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {

  PlatformBridgeFilterConfigSharedPtr filter_config =
      std::make_shared<PlatformBridgeFilterConfig>(extra_context.scopeOr(context), proto_config);
  Event::Dispatcher& dispatcher = context.mainThreadDispatcher();
  return [filter_config, &dispatcher](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<PlatformBridgeFilter>(filter_config, dispatcher));
  };
}

/**
 * Static registration for the PlatformBridge filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(PlatformBridgeFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace PlatformBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
