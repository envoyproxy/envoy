#include "source/extensions/filters/http/custom_response/config.h"

#include <memory>

#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"

// using envoy::extensions::filters::http::custom_response::v3::CustomResponse;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

namespace {

struct CustomResponseMatchAction
    : public Matcher::ActionBase<envoy::config::core::v3::TypedExtensionConfig> {
  explicit CustomResponseMatchAction(PolicySharedPtr policy) : policy_(policy) {}
  const PolicySharedPtr policy_;
};

struct CustomResponseActionFactoryContext {
  Server::Configuration::ServerFactoryContext& server_;
  Stats::StatName stats_prefix_;
};

class PolicyMatchActionFactory : public Matcher::ActionFactory<CustomResponseActionFactoryContext>,
                                 Logger::Loggable<Logger::Id::config> {
public:
  Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& config,
                        CustomResponseActionFactoryContext& context,
                        ProtobufMessage::ValidationVisitor& validation_visitor) override {
    auto& message =
        MessageUtil::downcastAndValidate<const envoy::config::core::v3::TypedExtensionConfig&>(
            config, validation_visitor);
    auto& factory = Envoy::Config::Utility::getAndCheckFactory<PolicyFactory>(message);
    auto policy_config = Envoy::Config::Utility::translateAnyToFactoryConfig(
        message.typed_config(), validation_visitor, factory);
    return [policy = factory.createPolicy(*policy_config, context.server_, context.stats_prefix_)] {
      return std::make_unique<CustomResponseMatchAction>(policy);
    };
  }
  std::string name() const override { return "custom_response_name"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::core::v3::TypedExtensionConfig>();
  }
};

REGISTER_FACTORY(PolicyMatchActionFactory,
                 Matcher::ActionFactory<CustomResponseActionFactoryContext>);

class CustomResponseMatchActionValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Http::HttpMatchingData> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Http::HttpMatchingData>&,
                                          absl::string_view) override {
    return absl::OkStatus();
  }
};

} // namespace

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
    Stats::StatName stats_prefix, Server::Configuration::FactoryContext& context)
    : FilterConfigBase(config, context.getServerFactoryContext()), stats_prefix_(stats_prefix) {}

FilterConfigBase::FilterConfigBase(
    const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
    Server::Configuration::ServerFactoryContext& context) {
  // Allow matcher to not be set, to allow for cases where we only have route or
  // virtual host specific configurations.
  if (config.has_custom_response_matcher()) {
    CustomResponseMatchActionValidationVisitor validation_visitor;
    CustomResponseActionFactoryContext action_factory_context{context, context.scope().prefix()};
    Matcher::MatchTreeFactory<Http::HttpMatchingData, CustomResponseActionFactoryContext> factory(
        action_factory_context, context, validation_visitor);
    matcher_ = factory.create(config.custom_response_matcher())();
  }
}

PolicySharedPtr FilterConfigBase::getPolicy(Http::ResponseHeaderMap& headers,
                                            const StreamInfo::StreamInfo& stream_info) const {
  if (!matcher_) {
    return PolicySharedPtr{};
  }

  Http::Matching::HttpMatchingDataImpl data(stream_info.downstreamAddressProvider());
  data.onResponseHeaders(headers);
  auto match = Matcher::evaluateMatch<Http::HttpMatchingData>(*matcher_, data);
  if (!match.result_) {
    return PolicySharedPtr{};
  }

  const auto result = match.result_();
  ASSERT(result->typeUrl() == CustomResponseMatchAction::staticTypeUrl());
  ASSERT(dynamic_cast<CustomResponseMatchAction*>(result.get()));
  return static_cast<const CustomResponseMatchAction*>(result.get())->policy_;
}

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
