#pragma once

#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_api_scrubber/v3/matcher_actions.pb.h"

#include "source/common/common/logger.h"
#include "source/common/http/utility.h"
#include "source/common/matcher/matcher.h"

#include "xds/type/matcher/v3/http_inputs.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {
using envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig;
using envoy::extensions::filters::http::proto_api_scrubber::v3::RestrictionConfig;
using Http::HttpMatchingData;
using Protobuf::Map;
using xds::type::matcher::v3::HttpAttributesCelMatchInput;
using ProtoApiScrubberRemoveFieldAction =
    envoy::extensions::filters::http::proto_api_scrubber::v3::RemoveFieldAction;
using FilteringMode = ProtoApiScrubberConfig::FilteringMode;
using MatchTreeHttpMatchingDataSharedPtr = Matcher::MatchTreeSharedPtr<HttpMatchingData>;
using StringPairToMatchTreeMap =
    absl::flat_hash_map<std::pair<std::string, std::string>, MatchTreeHttpMatchingDataSharedPtr>;
} // namespace

// The config for Proto API Scrubber filter. As a thread-safe class, it should be constructed only
// once and shared among filters for better performance.
class ProtoApiScrubberFilterConfig : public Logger::Loggable<Logger::Id::filter> {
public:
  // Creates and returns an instance of ProtoApiScrubberConfig.
  static absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>>
  create(const ProtoApiScrubberConfig& proto_config,
         Server::Configuration::FactoryContext& context);

  // Returns the match tree for a request payload field mask.
  MatchTreeHttpMatchingDataSharedPtr getRequestFieldMatcher(const std::string& method_name,
                                                            const std::string& field_mask) const;

  // Returns the match tree for a response payload field mask.
  MatchTreeHttpMatchingDataSharedPtr getResponseFieldMatcher(const std::string& method_name,
                                                             const std::string& field_mask) const;

  FilteringMode filteringMode() const { return filtering_mode_; }

private:
  // Private constructor to make sure that this class is used in a factory fashion using the
  // public `create` method.
  ProtoApiScrubberFilterConfig() = default;

  // Validates the filtering mode. Currently, only FilteringMode::OVERRIDE is supported.
  // For any unsupported FilteringMode, it returns absl::InvalidArgument.
  absl::Status validateFilteringMode(FilteringMode);

  // Validates the method name in the filter config.
  // The method should name should be of gRPC method name format i.e., '/package.service/method'
  // For any invalid method name, it returns absl::InvalidArgument with an appropriate error
  // message.
  absl::Status validateMethodName(absl::string_view);

  // Validates the field mask in the filter config.
  // The currently supported field mask is of format 'a.b.c'
  // Wildcards (e.g., '*') are not supported.
  // For any invalid field mask, it returns absl::InvalidArgument with an appropriate error message.
  absl::Status validateFieldMask(absl::string_view);

  // Initializes the request and response field mask maps using the proto_config.
  absl::Status initialize(const ProtoApiScrubberConfig& proto_config,
                          Envoy::Server::Configuration::FactoryContext& context);

  // Initializes the method's request and response restrictions using the restrictions configured
  // in the proto config.
  absl::Status initializeMethodRestrictions(absl::string_view method_name,
                                            StringPairToMatchTreeMap& field_restrictions,
                                            const Map<std::string, RestrictionConfig>& restrictions,
                                            Server::Configuration::FactoryContext& context);

  FilteringMode filtering_mode_;

  // A map from {method_name, field_mask} to the respective match tree for request fields.
  StringPairToMatchTreeMap request_field_restrictions_;

  // A map from {method_name, field_mask} to the respective match tree for response fields.
  StringPairToMatchTreeMap response_field_restrictions_;
};

// A class to validate the input type specified for the unified matcher in the config.
class MatcherInputValidatorVisitor : public Matcher::MatchTreeValidationVisitor<HttpMatchingData> {
public:
  // Validates whether the input type for the matcher is in the list of supported input types.
  // Currently, only CEL input type (i.e., HttpAttributesCelMatchInput) is supported.
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<HttpMatchingData>&,
                                          absl::string_view type_url) override {
    if (type_url == TypeUtil::descriptorFullNameToTypeUrl(
                        HttpAttributesCelMatchInput::descriptor()->full_name())) {
      return absl::OkStatus();
    }

    return absl::InvalidArgumentError(
        fmt::format("ProtoApiScrubber filter does not support matching on '{}'", type_url));
  }
};

// Action class for the RemoveFieldAction.
// RemoveFieldAction semantically denotes removing a field from request or response protobuf payload
// based on the field_mask and corresponding the restriction config provided.
class RemoveFieldAction : public Matcher::ActionBase<ProtoApiScrubberRemoveFieldAction> {};

// ActionFactory for the RemoveFieldAction.
class RemoveFilterActionFactory : public Matcher::ActionFactory<ProtoApiScrubberRemoveFieldAction> {
public:
  Matcher::ActionFactoryCb createActionFactoryCb(const Protobuf::Message&,
                                                 ProtoApiScrubberRemoveFieldAction&,
                                                 ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<RemoveFieldAction>(); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoApiScrubberRemoveFieldAction>();
  }

  std::string name() const override { return "removeFieldAction"; }
};

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
