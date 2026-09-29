#include "source/extensions/filters/http/ai_protocol_manager/config.h"

#include <memory>

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

absl::StatusOr<Http::FilterFactoryCb>
AiProtocolManagerFilterConfigFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager&
        proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {
  ExternalBufferFactorySharedPtr buffer_factory;
  if (proto_config.has_external_buffer_config()) {
    auto& factory = Config::Utility::getAndCheckFactory<ExternalBufferConfigFactory>(
        proto_config.external_buffer_config());
    auto message = Config::Utility::translateToFactoryConfig(
        proto_config.external_buffer_config(), context.messageValidationVisitor(), factory);
    buffer_factory = factory.createExternalBufferFactory(*message, context);
  } else {
    // One factory is shared by every stream on the chain. The in-memory
    // implementation is stateless, so a single shared instance is safe.
    buffer_factory = std::make_shared<InMemoryExternalBufferFactory>();
  }
  if (buffer_factory == nullptr) {
    return absl::InternalError("Failed to create ExternalBufferFactory");
  }
  absl::StatusOr<FilterConfigSharedPtr> config =
      FilterConfig::create(proto_config, context, extra_context.scopeOr(context));
  if (!config.ok()) {
    return config.status();
  }
  return [buffer_factory, config = std::move(config.value())](
             Http::FilterChainFactoryCallbacks& callbacks) -> void {
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
