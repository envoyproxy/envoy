#include "source/extensions/filters/http/ai_protocol_manager/config.h"

#include <memory>

#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

// Resolves external_buffer_config, defaulting to the in-memory store. Like the AI
// filter resolution in FilterConfig::create(), failures are returned, not thrown.
absl::StatusOr<ExternalBufferFactorySharedPtr> resolveExternalBufferFactory(
    const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager&
        proto_config,
    Server::Configuration::ServerFactoryContext& context) {
  if (!proto_config.has_external_buffer_config()) {
    // The in-memory implementation is stateless, so one instance serves every stream.
    return std::make_shared<InMemoryExternalBufferFactory>();
  }
  const auto& entry = proto_config.external_buffer_config();
  auto* factory =
      Config::Utility::getAndCheckFactory<ExternalBufferConfigFactory>(entry, /*is_optional=*/true);
  if (factory == nullptr) {
    return absl::InvalidArgumentError(
        fmt::format("ai_protocol_manager: unknown external buffer '{}' with type URL '{}'",
                    entry.name(), Config::Utility::getFactoryType(entry.typed_config())));
  }
  const ProtobufTypes::MessagePtr config = factory->createEmptyConfigProto();
  RETURN_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
      entry.typed_config(), context.messageValidationVisitor(), *config));
  ExternalBufferFactorySharedPtr buffer_factory =
      factory->createExternalBufferFactory(*config, context);
  if (buffer_factory == nullptr) {
    return absl::InternalError("Failed to create ExternalBufferFactory");
  }
  return buffer_factory;
}

// Builds the AI filter configurations a route carries. Each is built by the AI filter that
// registers its message type.
absl::StatusOr<AiFilterRouteConfigsConstSharedPtr> resolveAiFilterRouteConfigs(
    const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute&
        proto_config,
    Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& validator) {
  if (proto_config.typed_per_filter_config().empty()) {
    return nullptr;
  }
  auto filter_configs = std::make_shared<AiFilterRouteConfigs>();
  for (const auto& [name, typed_config] : proto_config.typed_per_filter_config()) {
    auto* factory = Config::Utility::getFactoryByType<AiFilterConfigFactory>(typed_config);
    const ProtobufTypes::MessagePtr config =
        factory != nullptr ? factory->createEmptyRouteConfigProto() : nullptr;
    if (config == nullptr) {
      return absl::InvalidArgumentError(
          fmt::format("ai_protocol_manager: no AI filter takes the per-route configuration '{}' "
                      "with type URL '{}'",
                      name, Config::Utility::getFactoryType(typed_config)));
    }
    RETURN_IF_NOT_OK(Config::Utility::translateOpaqueConfig(typed_config, validator, *config));
    absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr> filter_config =
        factory->createRouteConfig(*config, context, validator);
    RETURN_IF_NOT_OK_REF(filter_config.status());
    filter_configs->emplace(name, std::move(filter_config.value()));
  }
  return filter_configs;
}

} // namespace

absl::StatusOr<Http::FilterFactoryCb>
AiProtocolManagerFilterConfigFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager&
        proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {
  absl::StatusOr<ExternalBufferFactorySharedPtr> buffer_factory =
      resolveExternalBufferFactory(proto_config, context);
  RETURN_IF_NOT_OK_REF(buffer_factory.status());
  absl::StatusOr<FilterConfigSharedPtr> config = FilterConfig::create(
      proto_config, context, extra_context.scopeOr(context), std::move(buffer_factory.value()));
  RETURN_IF_NOT_OK_REF(config.status());
  return [config = std::move(config.value())](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(std::make_shared<AiProtocolManagerFilter>(config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
AiProtocolManagerFilterConfigFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute&
        proto_config,
    Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& validator) {
  absl::StatusOr<AiFilterRouteConfigsConstSharedPtr> filter_configs =
      resolveAiFilterRouteConfigs(proto_config, context, validator);
  RETURN_IF_NOT_OK_REF(filter_configs.status());
  absl::StatusOr<std::unique_ptr<RouteConfig>> route_config =
      RouteConfig::create(proto_config, std::move(filter_configs.value()));
  if (!route_config.ok()) {
    return route_config.status();
  }
  return Router::RouteSpecificFilterConfigConstSharedPtr(*std::move(route_config));
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
