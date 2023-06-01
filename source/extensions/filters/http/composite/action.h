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
    : public Logger::Loggable<Logger::Id::filter>,
      public Matcher::ActionFactory<Http::Matching::HttpFilterActionContext> {
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

    Envoy::Http::FilterFactoryCb callback = nullptr;
    try {
      if (context.server_factory_context_.has_value()) {
        callback = factory.createFilterServerFactoryFromProto(
            *message, context.stat_prefix_, context.server_factory_context_.value());
      }
    } catch (EnvoyException& e) {
      // Instead of causing the crash, the exception is handled gracefully: log the error and
      // fallback to creating the filter from factory context.
      ENVOY_LOG(debug,
                absl::StrCat(e.what(), ", fallback to creating the filter from factory context."));
    }

    if (callback == nullptr) {
      RELEASE_ASSERT(context.factory_context_.has_value(),
                     "The factory context must exist here to create the delegated filter");
      callback = factory.createFilterFactoryFromProto(*message, context.stat_prefix_,
                                                      context.factory_context_.value());
    }

    return [cb = std::move(callback)]() -> Matcher::ActionPtr {
      return std::make_unique<ExecuteFilterAction>(cb);
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::http::composite::v3::ExecuteFilterAction>();
  }
};

DECLARE_FACTORY(ExecuteFilterActionFactory);

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
