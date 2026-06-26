#include "source/extensions/filters/http/ai_protocol_manager/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

Http::FilterFactoryCb AiProtocolManagerFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager&,
    const std::string&, Server::Configuration::FactoryContext&) {
  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<AiProtocolManagerFilter>());
  };
}

/**
 * Static registration for the AI Protocol Manager filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AiProtocolManagerFilterConfigFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
