#include "library/common/extensions/filters/http/platform_bridge/config.h"

#include "library/common/extensions/filters/http/platform_bridge/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PlatformBridge {

Http::FilterFactoryCb PlatformBridgeFilterFactory::createFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::platform_bridge::PlatformBridge& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {

  PlatformBridgeFilterConfigSharedPtr filter_config =
      std::make_shared<PlatformBridgeFilterConfig>(proto_config);
  return [filter_config, &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        std::make_shared<PlatformBridgeFilter>(filter_config, context.mainThreadDispatcher()));
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
