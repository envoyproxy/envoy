#include "source/extensions/filters/http/composite/action.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

void ExecuteFilterAction::createFilters(Http::FilterChainFactoryCallbacks& callbacks) const {
  cb_(callbacks);
}

float ExecuteFilterActionFactory::getSampleRatio(
    const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
    Envoy::Runtime::Loader& runtime) {
  // In default case, if sample_percent is not populated, sample_ratio is 100%, i.e, all sampled.
  if (!composite_action.has_sample_percent()) {
    // 1, i.e, 100%,  means all sampled.
    return 1;
  }
  const auto& sample_percent = composite_action.sample_percent().default_value();
  float denominator = 100;
  switch (sample_percent.denominator()) {
  case envoy::type::v3::FractionalPercent::HUNDRED:
    denominator = 100;
    break;
  case envoy::type::v3::FractionalPercent::TEN_THOUSAND:
    denominator = 10000;
    break;
  case envoy::type::v3::FractionalPercent::MILLION:
    denominator = 1000000;
    break;
  default:
    throw EnvoyException(
        fmt::format("ExecuteFilterAction sample_percent config denominator setting "
                    "is invalid : {}. Valid range 0~2.",
                    sample_percent.denominator()));
  }

  float sample_ratio = static_cast<float>(sample_percent.numerator()) / denominator;
  if (sample_ratio > 1) {
    throw EnvoyException(fmt::format(
        "ExecuteFilterAction sample_percent config is invalid. sample_ratio={}(Numerator {} / "
        "Denominator {}). The valid range is 0~1.",
        sample_ratio, sample_percent.numerator(), denominator));
  }

  const std::string runtime_key = composite_action.sample_percent().runtime_key();
  if (!runtime_key.empty()) {
    // If sample_percent runtime_key is configured, it has to be a number between
    // [0, 100]. Using 101 to indicate this runtime_key is not configured.
    const uint64_t NO_SAMPLE_PERCENT_RUNTIME = 101;
    uint64_t sample_percent_runtime = runtime.snapshot().getInteger(runtime_key, NO_SAMPLE_PERCENT_RUNTIME);
    if (sample_percent_runtime < NO_SAMPLE_PERCENT_RUNTIME) {
      // runtime_key is configured with a valid number. Only in this case, using it to Override the default_value.
      sample_ratio = static_cast<float>(sample_percent_runtime) / 100;
    }
  }

  return sample_ratio;
}

bool ExecuteFilterActionFactory::isSampled(const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
                                           Random::RandomGenerator& random, Envoy::Runtime::Loader& runtime) {
  float sample_ratio = getSampleRatio(composite_action, runtime);
  UnitFloat sample_ratio_uf = UnitFloat(sample_ratio);

  // Perform dice roll. If negative, then skip the action.
  if (random.bernoulli(sample_ratio_uf)) {
    return true;
  } else {
    return false;
  }
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
    if (context.is_downstream_) {
      return createDynamicActionFactoryCbDownstream(composite_action, context);
    } else {
      return createDynamicActionFactoryCbUpstream(composite_action, context);
    }
  }

  if (context.is_downstream_) {
    return createStaticActionFactoryCbDownstream(composite_action, context, validation_visitor);
  } else {
    return createStaticActionFactoryCbUpstream(composite_action, context, validation_visitor);
  }
}

Matcher::ActionFactoryCb ExecuteFilterActionFactory::createDynamicActionFactoryCbDownstream(
    const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
    Http::Matching::HttpFilterActionContext& context) {
  if (!context.factory_context_.has_value() || !context.server_factory_context_.has_value()) {
    throw EnvoyException(
        fmt::format("Failed to get downstream factory context or server factory context."));
  }
  auto provider_manager =
      Envoy::Http::FilterChainUtility::createSingletonDownstreamFilterConfigProviderManager(
          context.server_factory_context_.value());
  return createDynamicActionFactoryCbTyped<Server::Configuration::FactoryContext>(
      composite_action, context, "http", context.factory_context_.value(), provider_manager);
}

Matcher::ActionFactoryCb ExecuteFilterActionFactory::createDynamicActionFactoryCbUpstream(
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
  return createDynamicActionFactoryCbTyped<Server::Configuration::UpstreamFactoryContext>(
      composite_action, context, "router upstream http", context.upstream_factory_context_.value(),
      provider_manager);
}

Matcher::ActionFactoryCb ExecuteFilterActionFactory::createAtionFactoryCbCommon(
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
  Random::RandomGenerator& random = context.server_factory_context_->api().randomGenerator();
  Envoy::Runtime::Loader& runtime = context.server_factory_context_->runtime();

  return [cb = std::move(callback), n = std::move(name), composite_action = std::move(composite_action),
          &random, &runtime, this]() -> Matcher::ActionPtr {
    if (isSampled(composite_action, random, runtime)) {
      return std::make_unique<ExecuteFilterAction>(cb, n);
    }
    return nullptr;
  };
}

Matcher::ActionFactoryCb ExecuteFilterActionFactory::createStaticActionFactoryCbDownstream(
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

  return createAtionFactoryCbCommon(composite_action, context, callback, true);
}

Matcher::ActionFactoryCb ExecuteFilterActionFactory::createStaticActionFactoryCbUpstream(
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

  return createAtionFactoryCbCommon(composite_action, context, callback, false);
}

REGISTER_FACTORY(ExecuteFilterActionFactory,
                 Matcher::ActionFactory<Http::Matching::HttpFilterActionContext>);
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
