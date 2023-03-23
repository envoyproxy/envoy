#pragma once

#include "envoy/extensions/filters/http/composite/v3/composite.pb.validate.h"

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
  explicit ExecuteFilterAction(Http::FilterFactoryCb cb) : cb_(std::move(cb)) {}

  void createFilters(Http::FilterChainFactoryCallbacks& callbacks) const;

private:
  Http::FilterFactoryCb cb_;
};

class ExecuteFilterActionFactory
    : public Matcher::ActionFactory<Http::Matching::HttpFilterActionContext> {
public:
  std::string name() const override { return "composite-action"; }
  Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& config,
                        Http::Matching::HttpFilterActionContext& context,
                        ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& composite_action = MessageUtil::downcastAndValidate<
        const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction&>(
        config, validation_visitor);

    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
            composite_action.typed_config());
    ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
        composite_action.typed_config().typed_config(), validation_visitor, factory);
    auto callback = factory.createFilterFactoryFromProto(*message, context.stat_prefix_,
                                                         context.factory_context_);
    return [cb = std::move(callback)]() -> Matcher::ActionPtr {
      return std::make_unique<ExecuteFilterAction>(cb);
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::http::composite::v3::ExecuteFilterAction>();
  }
};

DECLARE_FACTORY(ExecuteFilterActionFactory);

class ExecuteFilterMultiAction
    : public Matcher::ActionBase<
          envoy::extensions::filters::http::composite::v3::ExecuteFilterMultiAction> {
public:
  explicit ExecuteFilterMultiAction(std::vector<Http::FilterFactoryCb> cb_list)
      : cb_list_(std::move(cb_list)) {}

  void createFilters(std::function<void(Http::FilterFactoryCb&)> parse_wrapper) const;

private:
  std::vector<Http::FilterFactoryCb> cb_list_;
};

class ExecuteFilterMultiActionFactory
    : public Matcher::ActionFactory<Http::Matching::HttpFilterActionContext> {
public:
  std::string name() const override { return "composite-multi-action"; }
  Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& config,
                        Envoy::Http::Matching::HttpFilterActionContext& context,
                        ProtobufMessage::ValidationVisitor& validation) override {
    const auto& composite_action =
        MessageUtil::downcastAndValidate<const envoy::extensions::filters::http::composite::v3::ExecuteFilterMultiAction&>(config, validation);
    std::vector<Http::FilterFactoryCb> callback_list;
    for (auto typed_config : composite_action.typed_config()) {
      auto& factory =
          Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
              typed_config);
      ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
          typed_config.typed_config(), validation, factory);
      callback_list.push_back(factory.createFilterFactoryFromProto(*message, context.stat_prefix_,
                                                               context.factory_context_));
    }
    return [cb_list = std::move(callback_list)]() -> Matcher::ActionPtr {
      return std::make_unique<ExecuteFilterMultiAction>(cb_list);
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::http::composite::v3::ExecuteFilterMultiAction>();
  }
};

DECLARE_FACTORY(ExecuteFilterMultiActionFactory);
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
