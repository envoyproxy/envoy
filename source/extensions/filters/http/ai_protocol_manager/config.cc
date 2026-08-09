#include "source/extensions/filters/http/ai_protocol_manager/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

absl::StatusOr<Http::FilterFactoryCb>
AiProtocolManagerFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager&
        proto_config,
    const std::string&, DualInfo info, Server::Configuration::ServerFactoryContext&) {
  // One factory is shared by every stream on the chain. The in-memory
  // implementation is stateless, so a single shared instance is safe.
  auto buffer_factory = std::make_shared<InMemoryExternalBufferFactory>();
  auto config = std::make_shared<FilterConfig>(proto_config, info.scope);
  return [buffer_factory, config](Http::FilterChainFactoryCallbacks& callbacks) {
    // Both chains get the full stream filter (see #46385); upstream-placement
    // caveats are documented in ai_protocol_manager_filter.rst.
    callbacks.addStreamFilter(std::make_shared<AiProtocolManagerFilter>(*buffer_factory, config));
  };
}

/**
 * Static registration for the AI Protocol Manager filter (downstream and
 * upstream). @see RegisterFactory.
 */
REGISTER_FACTORY(AiProtocolManagerFilterConfigFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamAiProtocolManagerFilterConfigFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
