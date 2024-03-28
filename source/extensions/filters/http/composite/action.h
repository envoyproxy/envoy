#pragma once

#include "envoy/extensions/filters/http/composite/v3/composite.pb.validate.h"

#include "source/common/http/filter_chain_helper.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

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
  template<class FactoryCtx, class FilterCfgFactory>
  Matcher::ActionFactoryCb createActionFactoryCbTyped(
      const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction& composite_action,
      Http::Matching::HttpFilterActionContext& context,
      ProtobufMessage::ValidationVisitor& validation_visitor,
      OptRef<FactoryCtx> factory_context) {
    auto& factory =
        Config::Utility::getAndCheckFactory<FilterCfgFactory>(
            composite_action.typed_config());
    ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
        composite_action.typed_config().typed_config(), validation_visitor, factory);

    Envoy::Http::FilterFactoryCb callback = nullptr;

    // First, try to create the filter factory creation function from factory context (if exists).
    if (factory_context.has_value()) {
      auto callback_or_status = factory.createFilterFactoryFromProto(
          *message, context.stat_prefix_, factory_context.value());
      THROW_IF_STATUS_NOT_OK(callback_or_status, throw);
      callback = callback_or_status.value();
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
    std::string name = composite_action.typed_config().name();

    return [cb = std::move(callback), n = std::move(name)]() -> Matcher::ActionPtr {
      return std::make_unique<ExecuteFilterAction>(cb, n);
    };
  }
};

DECLARE_FACTORY(ExecuteFilterActionFactory);

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
