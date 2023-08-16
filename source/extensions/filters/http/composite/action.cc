#include "source/common/http/filter_chain_helper.h"
#include "source/extensions/filters/http/composite/action.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {
void ExecuteFilterAction::createFilters(Http::FilterChainFactoryCallbacks& callbacks) const {
  cb_(callbacks);
}

Matcher::ActionFactoryCb ExecuteFilterActionFactory::createActionFactoryCb(
    const Protobuf::Message& config, Http::Matching::HttpFilterActionContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& composite_action = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction&>(
      config, validation_visitor);
      if (composite_action.has_config_discovery()){
        auto config_discovery = composite_action.config_discovery();
            if (context.server_factory_context_.has_value()){
              Server::Configuration::ServerFactoryContext& server_factory_context = context.server_factory_context_.value();
              Server::Configuration::FactoryContext& factory_context = context.factory_context_.value();

  std::shared_ptr<Http::DownstreamFilterConfigProviderManager> filter_config_provider_manager =  Http::FilterChainUtility::createSingletonDownstreamFilterConfigProviderManager(
            server_factory_context);
            Envoy::Http::FilterFactoryCb callback = nullptr;
            std::unique_ptr<Envoy::Filter::DynamicFilterConfigProvider<Envoy::Filter::NamedHttpFilterFactoryCb>> provider = filter_config_provider_manager->createDynamicFilterConfigProvider(
        config_discovery, "name", server_factory_context, factory_context, server_factory_context.clusterManager(),
        false, "http", nullptr);
               return [cb = provider->config().value()]() -> Matcher::ActionPtr {
    return std::make_unique<ExecuteFilterAction>(cb);
  };
            }

      } else {

  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
          composite_action.typed_config());
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      composite_action.typed_config().typed_config(), validation_visitor, factory);

  Envoy::Http::FilterFactoryCb callback = nullptr;

  // First, try to create the filter factory creation function from factory context (if exists).
  if (context.factory_context_.has_value()) {
    callback = factory.createFilterFactoryFromProto(*message, context.stat_prefix_,
                                                    context.factory_context_.value());
  }

  // If above failed, try to create the filter factory creation function from server factory
  // context (if exists).
  if (callback == nullptr && context.server_factory_context_.has_value()) {
    callback = factory.createFilterFactoryFromProtoWithServerContext(
        *message, context.stat_prefix_, context.server_factory_context_.value());
  }

  if (callback == nullptr) {
    throw EnvoyException("Failed to get filter factory creation function");
  }

  return [cb = std::move(callback)]() -> Matcher::ActionPtr {
    return std::make_unique<ExecuteFilterAction>(cb);
  };
      }
}

REGISTER_FACTORY(ExecuteFilterActionFactory,
                 Matcher::ActionFactory<Http::Matching::HttpFilterActionContext>);
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
