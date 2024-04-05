#include "source/extensions/filters/http/composite/action.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

using HttpExtensionConfigProviderSharedPtr = std::shared_ptr<
    Config::DynamicExtensionConfigProvider<Envoy::Filter::NamedHttpFilterFactoryCb>>;

void ExecuteFilterAction::createFilters(Http::FilterChainFactoryCallbacks& callbacks) const {
  cb_(callbacks);
}

Matcher::ActionFactoryCb ExecuteFilterActionFactory::createActionFactoryCb(
    const Protobuf::Message& config, Http::Matching::HttpFilterActionContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& composite_action = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction&>(
      config, validation_visitor);

  if (composite_action.has_dynamic_config() && composite_action.has_typed_config()) {
    throw EnvoyException(
        fmt::format("Error: Only one of `dynamic_config` or `typed_config` can be set."));
  }

  if (composite_action.has_dynamic_config()) {
    if (!context.is_downstream_) {
      throw EnvoyException(fmt::format("When composite filter is in upstream, the composite action "
                                       "config must not be dynamic."));
    }

    if (!context.factory_context_.has_value() || !context.server_factory_context_.has_value()) {
      throw EnvoyException(fmt::format("Failed to get factory context or server factory context."));
    }
    std::string name = composite_action.dynamic_config().name();
    // Create a dynamic filter config provider and register it with the server factory context.
    auto config_discovery = composite_action.dynamic_config().config_discovery();
    Server::Configuration::FactoryContext& factory_context = context.factory_context_.value();
    Server::Configuration::ServerFactoryContext& server_factory_context =
        context.server_factory_context_.value();
    auto provider_manager =
        Envoy::Http::FilterChainUtility::createSingletonDownstreamFilterConfigProviderManager(
            server_factory_context);
    HttpExtensionConfigProviderSharedPtr provider =
        provider_manager->createDynamicFilterConfigProvider(
            config_discovery, composite_action.dynamic_config().name(), server_factory_context,
            factory_context, server_factory_context.clusterManager(), false, "http", nullptr);
    return [provider = std::move(provider), n = std::move(name)]() -> Matcher::ActionPtr {
      auto config_value = provider->config();
      if (config_value.has_value()) {
        auto factory_cb = config_value.value().get().factory_cb;
        return std::make_unique<ExecuteFilterAction>(factory_cb, n);
      }
      // There is no dynamic config available. Apply missing config filter.
      auto factory_cb = Envoy::Http::MissingConfigFilterFactory;
      return std::make_unique<ExecuteFilterAction>(factory_cb, n);
    };
  }

  if (context.is_downstream_) {
    return createActionFactoryCbDownstream(composite_action, context, validation_visitor);
  } else {
    return createActionFactoryCbUpstream(composite_action, context, validation_visitor);
  }
}

Matcher::ActionFactoryCb ExecuteFilterActionFactory::createActionFactoryCbDownstream(
    const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
    Http::Matching::HttpFilterActionContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
          composite_action.typed_config());
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      composite_action.typed_config().typed_config(), validation_visitor, factory);

  Envoy::Http::FilterFactoryCb callback = nullptr;

  // First, try to create the filter factory creation function from factory context (if exists).
  if (context.factory_context_.has_value()) {
    auto callback_or_status = factory.createFilterFactoryFromProto(
        *message, context.stat_prefix_, context.factory_context_.value());
    THROW_IF_STATUS_NOT_OK(callback_or_status, throw);
    callback = callback_or_status.value();
  }

  // If above failed, for downstream case, try to create the filter factory creation function
  // from server factory context if exists.
  if (callback == nullptr && context.server_factory_context_.has_value()) {
    callback = factory.createFilterFactoryFromProtoWithServerContext(
        *message, context.stat_prefix_, context.server_factory_context_.value());
  }

  if (callback == nullptr) {
    throw EnvoyException("Failed to get filter factory creation function");
  }
  std::string name = composite_action.typed_config().name();

  return [cb = std::move(callback), n = std::move(name)]() -> Matcher::ActionPtr {
    return std::make_unique<ExecuteFilterAction>(cb, n);
  };
}

Matcher::ActionFactoryCb ExecuteFilterActionFactory::createActionFactoryCbUpstream(
    const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
    Http::Matching::HttpFilterActionContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::UpstreamHttpFilterConfigFactory>(
          composite_action.typed_config());
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      composite_action.typed_config().typed_config(), validation_visitor, factory);

  Envoy::Http::FilterFactoryCb callback = nullptr;

  // First, try to create the filter factory creation function from upstream factory context (if
  // exists).
  if (context.upstream_factory_context_.has_value()) {
    auto callback_or_status = factory.createFilterFactoryFromProto(
        *message, context.stat_prefix_, context.upstream_factory_context_.value());
    THROW_IF_STATUS_NOT_OK(callback_or_status, throw);
    callback = callback_or_status.value();
  }

  if (callback == nullptr) {
    throw EnvoyException("Failed to get filter factory creation function");
  }
  std::string name = composite_action.typed_config().name();

  return [cb = std::move(callback), n = std::move(name)]() -> Matcher::ActionPtr {
    return std::make_unique<ExecuteFilterAction>(cb, n);
  };
}

REGISTER_FACTORY(ExecuteFilterActionFactory,
                 Matcher::ActionFactory<Http::Matching::HttpFilterActionContext>);
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
