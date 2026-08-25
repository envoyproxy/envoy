#include "source/extensions/filters/http/ai_protocol_manager/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

absl::StatusOr<Http::FilterFactoryCb>
AiProtocolManagerFilterConfigFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager&
        proto_config,
    Server::Configuration::ServerFactoryContext&, Server::Configuration::ExtraFactoryContext&) {
  // One factory is shared by every stream on the chain. The in-memory
  // implementation is stateless, so a single shared instance is safe.
  auto buffer_factory = std::make_shared<InMemoryExternalBufferFactory>();
  auto config = std::make_shared<const FilterConfig>(proto_config);
  return [buffer_factory, config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<AiProtocolManagerFilter>(*buffer_factory, config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
AiProtocolManagerFilterConfigFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute&
        proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<const RouteConfig>(proto_config);
}

/**
 * Static registration for the AI Protocol Manager filter as a downstream and an
 * upstream HTTP filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AiProtocolManagerFilterConfigFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamAiProtocolManagerFilterConfigFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
