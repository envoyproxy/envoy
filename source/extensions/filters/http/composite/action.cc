#include "source/extensions/filters/http/composite/action.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

void ExecuteFilterAction::createFilters(Http::FilterChainFactoryCallbacks& callbacks) const {
  if (actionSkip()) {
    return;
  }

  // Handle filter chain mode.
  if (is_filter_chain_) {
    for (const auto& factory_cb : filter_factories_) {
      factory_cb(callbacks);
    }
    return;
  }

  // Handle single filter mode.
  if (auto config_value = config_provider_(); config_value.has_value()) {
    (*config_value)(callbacks);
    return;
  }
  // There is no dynamic config available. Apply missing config filter.
  Envoy::Http::MissingConfigFilterFactory(callbacks);
}

const std::string& ExecuteFilterAction::actionName() const { return name_; }

bool ExecuteFilterAction::actionSkip() const {
  return sample_.has_value()
             ? !runtime_.snapshot().featureEnabled(sample_->runtime_key(), sample_->default_value())
             : false;
}

Matcher::ActionConstSharedPtr
ExecuteFilterActionFactory::createAction(const Protobuf::Message& config,
                                         Http::Matching::HttpFilterActionContext& context,
                                         ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& composite_action = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction&>(
      config, validation_visitor);

  // Filter chain takes priority over typed_config and dynamic_config.
  if (composite_action.has_filter_chain()) {
    return createFilterChainAction(composite_action, context, validation_visitor);
  }

  if (composite_action.has_dynamic_config() && composite_action.has_typed_config()) {
    throw EnvoyException(
        fmt::format("Error: Only one of `dynamic_config` or `typed_config` can be set."));
  }

  if (composite_action.has_dynamic_config()) {
    if (context.is_downstream_) {
      return createDynamicActionDownstream(composite_action, context);
    } else {
      return createDynamicActionUpstream(composite_action, context);
    }
  }

  if (context.is_downstream_) {
    return createStaticActionDownstream(composite_action, context, validation_visitor);
  } else {
    return createStaticActionUpstream(composite_action, context, validation_visitor);
  }
}

Matcher::ActionConstSharedPtr ExecuteFilterActionFactory::createDynamicActionDownstream(
    const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
    Http::Matching::HttpFilterActionContext& context) {
  if (!context.factory_context_.has_value() || !context.server_factory_context_.has_value()) {
    throw EnvoyException(
        fmt::format("Failed to get downstream factory context or server factory context."));
  }
  auto provider_manager =
      Envoy::Http::FilterChainUtility::createSingletonDownstreamFilterConfigProviderManager(
          context.server_factory_context_.value());
  return createDynamicActionTyped<Server::Configuration::FactoryContext>(
      composite_action, context, "http", context.factory_context_.value(), provider_manager);
}

Matcher::ActionConstSharedPtr ExecuteFilterActionFactory::createDynamicActionUpstream(
    const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
    Http::Matching::HttpFilterActionContext& context) {
  if (!context.upstream_factory_context_.has_value() ||
      !context.server_factory_context_.has_value()) {
    throw EnvoyException(
        fmt::format("Failed to get upstream factory context or server factory context."));
  }
  auto provider_manager =
      Envoy::Http::FilterChainUtility::createSingletonUpstreamFilterConfigProviderManager(
          context.server_factory_context_.value());
  return createDynamicActionTyped<Server::Configuration::UpstreamFactoryContext>(
      composite_action, context, "router upstream http", context.upstream_factory_context_.value(),
      provider_manager);
}

Matcher::ActionConstSharedPtr ExecuteFilterActionFactory::createActionCommon(
    const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
    Http::Matching::HttpFilterActionContext& context, Envoy::Http::FilterFactoryCb& callback,
    bool is_downstream) {
  const std::string stream_str = is_downstream ? "downstream" : "upstream";

  if (callback == nullptr) {
    throw EnvoyException(
        fmt::format("Failed to get {} filter factory creation function", stream_str));
  }
  std::string name = composite_action.typed_config().name();
  ASSERT(context.server_factory_context_ != absl::nullopt);
  Envoy::Runtime::Loader& runtime = context.server_factory_context_->runtime();

  return std::make_shared<ExecuteFilterAction>(
      [cb = std::move(callback)]() mutable -> OptRef<Http::FilterFactoryCb> { return cb; }, name,
      composite_action.has_sample_percent()
          ? absl::make_optional<envoy::config::core::v3::RuntimeFractionalPercent>(
                composite_action.sample_percent())
          : absl::nullopt,
      runtime);
}

Matcher::ActionConstSharedPtr ExecuteFilterActionFactory::createStaticActionDownstream(
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
    THROW_IF_NOT_OK_REF(callback_or_status.status());
    callback = callback_or_status.value();
  }

  // If above failed, for downstream case, try to create the filter factory creation function
  // from server factory context if exists.
  if (callback == nullptr && context.server_factory_context_.has_value()) {
    callback = factory.createFilterFactoryFromProtoWithServerContext(
        *message, context.stat_prefix_, context.server_factory_context_.value());
  }

  return createActionCommon(composite_action, context, callback, true);
}

Matcher::ActionConstSharedPtr ExecuteFilterActionFactory::createStaticActionUpstream(
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
    THROW_IF_NOT_OK_REF(callback_or_status.status());
    callback = callback_or_status.value();
  }

  return createActionCommon(composite_action, context, callback, false);
}

Matcher::ActionConstSharedPtr ExecuteFilterActionFactory::createFilterChainAction(
    const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
    Http::Matching::HttpFilterActionContext& context,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& filter_chain_config = composite_action.filter_chain();
  if (filter_chain_config.typed_config().empty()) {
    throw EnvoyException("Error: filter_chain must contain at least one filter.");
  }

  FilterFactoryCbList filter_factories;
  filter_factories.reserve(filter_chain_config.typed_config().size());

  for (const auto& filter_config : filter_chain_config.typed_config()) {
    Http::FilterFactoryCb callback = nullptr;

    if (context.is_downstream_) {
      // For downstream filters.
      auto& factory =
          Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
              filter_config);
      ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
          filter_config.typed_config(), validation_visitor, factory);

      // First, try to create from factory context.
      if (context.factory_context_.has_value()) {
        auto callback_or_status = factory.createFilterFactoryFromProto(
            *message, context.stat_prefix_, context.factory_context_.value());
        THROW_IF_NOT_OK_REF(callback_or_status.status());
        callback = callback_or_status.value();
      }

      // If above failed, try server factory context.
      if (callback == nullptr && context.server_factory_context_.has_value()) {
        callback = factory.createFilterFactoryFromProtoWithServerContext(
            *message, context.stat_prefix_, context.server_factory_context_.value());
      }

      if (callback == nullptr) {
        throw EnvoyException(fmt::format(
            "Failed to create downstream filter factory for filter '{}'", filter_config.name()));
      }
    } else {
      // For upstream filters.
      auto& factory = Config::Utility::getAndCheckFactory<
          Server::Configuration::UpstreamHttpFilterConfigFactory>(filter_config);
      ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
          filter_config.typed_config(), validation_visitor, factory);

      if (context.upstream_factory_context_.has_value()) {
        auto callback_or_status = factory.createFilterFactoryFromProto(
            *message, context.stat_prefix_, context.upstream_factory_context_.value());
        THROW_IF_NOT_OK_REF(callback_or_status.status());
        callback = callback_or_status.value();
      }

      if (callback == nullptr) {
        throw EnvoyException(fmt::format("Failed to create upstream filter factory for filter '{}'",
                                         filter_config.name()));
      }
    }

    filter_factories.push_back(std::move(callback));
  }

  ASSERT(context.server_factory_context_ != absl::nullopt);
  Envoy::Runtime::Loader& runtime = context.server_factory_context_->runtime();

  // Use "filter_chain" as the action name for filter chains.
  return std::make_shared<ExecuteFilterAction>(
      std::move(filter_factories), "filter_chain",
      composite_action.has_sample_percent()
          ? absl::make_optional<envoy::config::core::v3::RuntimeFractionalPercent>(
                composite_action.sample_percent())
          : absl::nullopt,
      runtime);
}

REGISTER_FACTORY(ExecuteFilterActionFactory,
                 Matcher::ActionFactory<Http::Matching::HttpFilterActionContext>);
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
