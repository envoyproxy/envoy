#pragma once

#include "envoy/extensions/filters/http/composite/v3/composite.pb.validate.h"

#include "source/common/http/filter_chain_helper.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

using HttpExtensionConfigProviderSharedPtr =
    std::shared_ptr<Config::DynamicExtensionConfigProvider<Envoy::Filter::HttpFilterFactoryCb>>;

class ExecuteFilterAction
    : public Matcher::ActionBase<
          envoy::extensions::filters::http::composite::v3::ExecuteFilterAction> {
public:
  using FilterConfigProvider = std::function<OptRef<Http::FilterFactoryCb>()>;

  explicit ExecuteFilterAction(
      FilterConfigProvider config_provider, const std::string& name,
      const absl::optional<envoy::config::core::v3::RuntimeFractionalPercent>& sample,
      Runtime::Loader& runtime)
      : config_provider_(std::move(config_provider)), name_(name), sample_(sample),
        runtime_(runtime) {}

  void createFilters(Http::FilterChainFactoryCallbacks& callbacks) const;

  const std::string& actionName() const;

  bool actionSkip() const;

private:
  FilterConfigProvider config_provider_;
  const std::string name_;
  const absl::optional<envoy::config::core::v3::RuntimeFractionalPercent> sample_;
  Runtime::Loader& runtime_;
};

class ExecuteFilterActionFactory
    : public Logger::Loggable<Logger::Id::filter>,
      public Matcher::ActionFactory<Http::Matching::HttpFilterActionContext> {
public:
  std::string name() const override { return "composite-action"; }

  Matcher::ActionConstSharedPtr
  createAction(const Protobuf::Message& config, Http::Matching::HttpFilterActionContext& context,
               ProtobufMessage::ValidationVisitor& validation_visitor) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::http::composite::v3::ExecuteFilterAction>();
  }

private:
  Matcher::ActionConstSharedPtr createActionCommon(
      const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
      Http::Matching::HttpFilterActionContext& context, Envoy::Http::FilterFactoryCb& callback,
      bool is_downstream);

  template <class FactoryCtx, class FilterCfgProviderMgr>
  Matcher::ActionConstSharedPtr createDynamicActionTyped(
      const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
      Http::Matching::HttpFilterActionContext& context, const std::string& filter_chain_type,
      FactoryCtx& factory_context, std::shared_ptr<FilterCfgProviderMgr>& provider_manager) {
    std::string name = composite_action.dynamic_config().name();
    // Create a dynamic filter config provider and register it with the server factory context.
    auto config_discovery = composite_action.dynamic_config().config_discovery();
    Server::Configuration::ServerFactoryContext& server_factory_context =
        context.server_factory_context_.value();
    HttpExtensionConfigProviderSharedPtr provider =
        provider_manager->createDynamicFilterConfigProvider(
            config_discovery, name, server_factory_context, factory_context,
            server_factory_context.clusterManager(), false, filter_chain_type, nullptr);

    Envoy::Runtime::Loader& runtime = context.server_factory_context_->runtime();

    return std::make_shared<ExecuteFilterAction>(
        [provider]() -> OptRef<Http::FilterFactoryCb> { return provider->config(); }, name,
        composite_action.has_sample_percent()
            ? absl::make_optional<envoy::config::core::v3::RuntimeFractionalPercent>(
                  composite_action.sample_percent())
            : absl::nullopt,
        runtime);
  }

  Matcher::ActionConstSharedPtr createDynamicActionDownstream(
      const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
      Http::Matching::HttpFilterActionContext& context);

  Matcher::ActionConstSharedPtr createDynamicActionUpstream(
      const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
      Http::Matching::HttpFilterActionContext& context);

  Matcher::ActionConstSharedPtr createStaticActionDownstream(
      const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
      Http::Matching::HttpFilterActionContext& context,
      ProtobufMessage::ValidationVisitor& validation_visitor);

  Matcher::ActionConstSharedPtr createStaticActionUpstream(
      const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
      Http::Matching::HttpFilterActionContext& context,
      ProtobufMessage::ValidationVisitor& validation_visitor);
};

DECLARE_FACTORY(ExecuteFilterActionFactory);

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
