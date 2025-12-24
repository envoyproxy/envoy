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

// Vector of filter factory callbacks for filter chain support.
using FilterFactoryCbList = std::vector<Http::FilterFactoryCb>;

// A map of named filter chains that have been pre-compiled at configuration time.
// Each entry maps a filter chain name to a list of filter factory callbacks.
using NamedFilterChainFactoryMap =
    absl::flat_hash_map<std::string, std::vector<Http::FilterFactoryCb>>;
using NamedFilterChainFactoryMapSharedPtr = std::shared_ptr<NamedFilterChainFactoryMap>;

class ExecuteFilterAction
    : public Matcher::ActionBase<
          envoy::extensions::filters::http::composite::v3::ExecuteFilterAction> {
public:
  using FilterConfigProvider = std::function<OptRef<Http::FilterFactoryCb>()>;

  // Constructor for single filter which is either typed_config or dynamic_config.
  explicit ExecuteFilterAction(
      FilterConfigProvider config_provider, const std::string& name,
      const absl::optional<envoy::config::core::v3::RuntimeFractionalPercent>& sample,
      Runtime::Loader& runtime)
      : config_provider_(std::move(config_provider)), name_(name), sample_(sample),
        runtime_(runtime), is_filter_chain_(false) {}

  // Constructor for filter chain (inline filter_chain).
  explicit ExecuteFilterAction(
      FilterFactoryCbList filter_factories, const std::string& name,
      const absl::optional<envoy::config::core::v3::RuntimeFractionalPercent>& sample,
      Runtime::Loader& runtime)
      : filter_factories_(std::move(filter_factories)), name_(name), sample_(sample),
        runtime_(runtime), is_filter_chain_(true) {}

  // Constructor for named filter chain lookup.
  explicit ExecuteFilterAction(
      const std::string& filter_chain_name,
      const absl::optional<envoy::config::core::v3::RuntimeFractionalPercent>& sample,
      Runtime::Loader& runtime)
      : name_(filter_chain_name), sample_(sample), runtime_(runtime), is_filter_chain_(false),
        is_named_filter_chain_lookup_(true) {}

  void createFilters(Http::FilterChainFactoryCallbacks& callbacks) const;

  const std::string& actionName() const;

  bool actionSkip() const;

  // Returns true if this action executes a filter chain rather than a single filter.
  bool isFilterChain() const { return is_filter_chain_; }

  // Returns true if this action requires a runtime lookup of a named filter chain.
  bool isNamedFilterChainLookup() const { return is_named_filter_chain_lookup_; }

  // Returns the filter chain name for named filter chain lookup actions.
  // Only valid when isNamedFilterChainLookup() returns true.
  const std::string& filterChainName() const { return name_; }

private:
  // Used for single filter mode which is either typed_config or dynamic_config.
  FilterConfigProvider config_provider_;
  // Used for filter chain mode.
  FilterFactoryCbList filter_factories_;
  const std::string name_;
  const absl::optional<envoy::config::core::v3::RuntimeFractionalPercent> sample_;
  Runtime::Loader& runtime_;
  const bool is_filter_chain_;
  const bool is_named_filter_chain_lookup_{false};
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

  // Create an action for filter chain configuration.
  Matcher::ActionConstSharedPtr createFilterChainAction(
      const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
      Http::Matching::HttpFilterActionContext& context,
      ProtobufMessage::ValidationVisitor& validation_visitor);
};

DECLARE_FACTORY(ExecuteFilterActionFactory);

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
