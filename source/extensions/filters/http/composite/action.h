#pragma once

#include "envoy/extensions/filters/http/composite/v3/composite.pb.validate.h"

#include "source/common/http/filter_chain_helper.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

using HttpExtensionConfigProviderSharedPtr = std::shared_ptr<
    Config::DynamicExtensionConfigProvider<Envoy::Filter::NamedHttpFilterFactoryCb>>;

class ExecuteFilterAction
    : public Matcher::ActionBase<
          envoy::extensions::filters::http::composite::v3::ExecuteFilterAction> {
public:
  explicit ExecuteFilterAction(Http::FilterFactoryCb cb, const std::string& name)
      : cb_(std::move(cb)), name_(name) {}

  void createFilters(Http::FilterChainFactoryCallbacks& callbacks) const;

  const std::string& actionName() const { return name_; }

private:
  Http::FilterFactoryCb cb_;
  const std::string name_;
};

class ExecuteFilterActionFactory
    : public Logger::Loggable<Logger::Id::filter>,
      public Matcher::ActionFactory<Http::Matching::HttpFilterActionContext> {
public:
  std::string name() const override { return "composite-action"; }

  Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& config,
                        Http::Matching::HttpFilterActionContext& context,
                        ProtobufMessage::ValidationVisitor& validation_visitor) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::http::composite::v3::ExecuteFilterAction>();
  }

private:
  template <class FactoryCtx, class FilterCfgProviderMgr>
  Matcher::ActionFactoryCb createDynamicActionFactoryCbTyped(
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

  Matcher::ActionFactoryCb createDynamicActionFactoryCbDownstream(
      const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
      Http::Matching::HttpFilterActionContext& context);

  Matcher::ActionFactoryCb createDynamicActionFactoryCbUpstream(
      const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
      Http::Matching::HttpFilterActionContext& context);

  Matcher::ActionFactoryCb createStaticActionFactoryCbDownstream(
      const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
      Http::Matching::HttpFilterActionContext& context,
      ProtobufMessage::ValidationVisitor& validation_visitor);

  Matcher::ActionFactoryCb createStaticActionFactoryCbUpstream(
      const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
      Http::Matching::HttpFilterActionContext& context,
      ProtobufMessage::ValidationVisitor& validation_visitor);
};

DECLARE_FACTORY(ExecuteFilterActionFactory);

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
