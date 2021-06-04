#pragma once

#include "envoy/extensions/filters/http/composite/v3/composite.pb.validate.h"

#include "common/matcher/matcher.h"

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

class ExecuteFilterActionFactory : public Matcher::ActionFactory {
public:
  std::string name() const override { return "composite-action"; }
  Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& config, const std::string& stat_prefix,
                        Server::Configuration::FactoryContext& factory_context) override {
    const auto& composite_action = MessageUtil::downcastAndValidate<
        const envoy::extensions::filters::http::composite::v3::ExecuteFilterAction&>(
        config, factory_context.messageValidationVisitor());

    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
            composite_action.typed_config());
    ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
        composite_action.typed_config().typed_config(), factory_context.messageValidationVisitor(),
        factory);
    auto callback = factory.createFilterFactoryFromProto(*message, stat_prefix, factory_context);
    return [cb = std::move(callback)]() -> Matcher::ActionPtr {
      return std::make_unique<ExecuteFilterAction>(cb);
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::http::composite::v3::ExecuteFilterAction>();
  }
};
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
