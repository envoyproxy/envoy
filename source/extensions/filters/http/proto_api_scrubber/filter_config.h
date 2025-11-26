#pragma once

#include <memory>
#include <string>
#include <utility>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_api_scrubber/v3/matcher_actions.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"
#include "source/common/http/utility.h"
#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "grpc_transcoding/type_helper.h"
#include "xds/type/matcher/v3/http_inputs.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {
using envoy::extensions::filters::http::proto_api_scrubber::v3::MessageRestrictions;
using envoy::extensions::filters::http::proto_api_scrubber::v3::MethodRestrictions;
using envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig;
using envoy::extensions::filters::http::proto_api_scrubber::v3::RestrictionConfig;
using google::grpc::transcoding::TypeHelper;
using Http::HttpMatchingData;
using Protobuf::Map;
using Protobuf::MethodDescriptor;
using xds::type::matcher::v3::HttpAttributesCelMatchInput;
using ProtoApiScrubberRemoveFieldAction =
    envoy::extensions::filters::http::proto_api_scrubber::v3::RemoveFieldAction;
using FilteringMode = ProtoApiScrubberConfig::FilteringMode;
using MatchTreeHttpMatchingDataSharedPtr = Matcher::MatchTreeSharedPtr<HttpMatchingData>;
using StringPairToMatchTreeMap =
    absl::flat_hash_map<std::pair<std::string, std::string>, MatchTreeHttpMatchingDataSharedPtr>;
using TypeFinder = std::function<const Envoy::Protobuf::Type*(const std::string&)>;
} // namespace

// The config for Proto API Scrubber filter. As a thread-safe class, it should be constructed only
// once and shared among filters for better performance.
class ProtoApiScrubberFilterConfig : public Logger::Loggable<Logger::Id::filter> {
public:
  // Creates and returns an instance of ProtoApiScrubberConfig.
  static absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>>
  create(const ProtoApiScrubberConfig& proto_config,
         Server::Configuration::FactoryContext& context);

  virtual ~ProtoApiScrubberFilterConfig() = default;

  /**
   * Returns the match tree associated with a specific field in a request message.
   *
   * This method is called to retrieve the CEL matcher configuration that determines
   * whether a given field (identified by `field_mask`) within a specific gRPC
   * request (identified by `method_name`) should be scrubbed.
   *
   * @param method_name The full gRPC method name (e.g., "package.service/Method").
   * @param field_mask The field mask of the field in the request payload to check (e.g.,
   * "user.address.street").
   * @return A MatchTreeHttpMatchingDataSharedPtr if a matcher is configured for
   * the specific method and field mask.
   * Returns `nullptr` if no restriction is configured for this combination.
   */
  virtual MatchTreeHttpMatchingDataSharedPtr
  getRequestFieldMatcher(const std::string& method_name, const std::string& field_mask) const;

  /**
   * Returns the match tree associated with a specific field in a response message.
   *
   * This method is called to retrieve the CEL matcher configuration that determines
   * whether a given field (identified by `field_mask`) within a specific gRPC
   * response (identified by `method_name`) should be scrubbed.
   *
   * @param method_name The full gRPC method name (e.g., "package.service/Method").
   * @param field_mask The field mask of the field in the response payload to check (e.g.,
   * "user.address.street").
   * @return A MatchTreeHttpMatchingDataSharedPtr if a matcher is configured for
   * the specific method and field mask.
   * Returns `nullptr` if no restriction is configured for this combination.
   */
  virtual MatchTreeHttpMatchingDataSharedPtr
  getResponseFieldMatcher(const std::string& method_name, const std::string& field_mask) const;

  /**
   * Returns the match tree associated with an entire method.
   * @param method_name The full gRPC method name (e.g., "/package.service.Method").
   * @return A MatchTreeHttpMatchingDataSharedPtr if a method-level matcher is configured.
   * Returns `nullptr` otherwise.
   */
  virtual MatchTreeHttpMatchingDataSharedPtr getMethodMatcher(const std::string& method_name) const;

  /**
   * Returns the match tree associated with a specific message type.
   * @param message_name The fully qualified message name (e.g., "package.MyMessage").
   * @return A MatchTreeHttpMatchingDataSharedPtr if a message-level matcher is configured.
   * Returns `nullptr` otherwise.
   */
  virtual MatchTreeHttpMatchingDataSharedPtr
  getMessageMatcher(const std::string& message_name) const;

  /**
   * Resolves the human-readable name of a specific enum value.
   *
   * @param enum_type_name The fully qualified name of the enum type (e.g., "package.Status").
   * @param enum_value The integer value of the enum (e.g., 99).
   * @return The string name of the enum value (e.g., "DEBUG_MODE").
   * Returns empty string if the type or value is not found.
   */
  virtual absl::StatusOr<absl::string_view> getEnumName(absl::string_view enum_type_name,
                                                        int enum_value) const;

  // Returns a constant reference to the type finder which resolves type URL string to the
  // corresponding `Protobuf::Type*`.
  const TypeFinder& getTypeFinder() const { return *type_finder_; };

  // Returns the request type of the method.
  absl::StatusOr<const Protobuf::Type*> getRequestType(const std::string& method_name) const;

  // Returns the response type of the method.
  absl::StatusOr<const Protobuf::Type*> getResponseType(const std::string& method_name) const;

  FilteringMode filteringMode() const { return filtering_mode_; }

private:
  friend class MockProtoApiScrubberFilterConfig;
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

  // Validates the fully qualified message name.
  absl::Status validateMessageName(absl::string_view message_name);

  // Validates the field mask in the filter config.
  // The currently supported field mask is of format 'a.b.c'
  // Wildcards (e.g., '*') are not supported.
  // For any invalid field mask, it returns absl::InvalidArgument with an appropriate error message.
  absl::Status validateFieldMask(absl::string_view);

  // Initializes the request and response field mask maps using the proto_config.
  absl::Status initialize(const ProtoApiScrubberConfig& proto_config,
                          Envoy::Server::Configuration::FactoryContext& context);

  // Initializes the descriptor pool from the provided 'data_source'.
  absl::Status initializeDescriptorPool(Api::Api& api,
                                        const ::envoy::config::core::v3::DataSource& data_source);

  // Initializes the type utilities (e.g., type helper, type finder, etc.).
  void initializeTypeUtils();

  // Initializes the method's request and response restrictions using the restrictions configured
  // in the proto config.
  absl::Status
  initializeMethodFieldRestrictions(absl::string_view method_name,
                                    StringPairToMatchTreeMap& field_restrictions,
                                    const Map<std::string, RestrictionConfig>& restrictions,
                                    Server::Configuration::FactoryContext& context);

  // Initializes the method-level restrictions.
  absl::Status
  initializeMethodLevelRestrictions(absl::string_view method_name,
                                    const MethodRestrictions& method_config,
                                    Envoy::Server::Configuration::FactoryContext& context);

  // Initializes the message-level restrictions.
  absl::Status
  initializeMessageRestrictions(const Map<std::string, MessageRestrictions>& message_configs,
                                Envoy::Server::Configuration::FactoryContext& context);

  // Returns method descriptor by looking up the `descriptor_pool_`.
  // If the method doesn't exist in the `descriptor_pool`, it returns absl::InvalidArgument error.
  absl::StatusOr<const MethodDescriptor*> getMethodDescriptor(const std::string& method_name) const;

  FilteringMode filtering_mode_;

  std::unique_ptr<const Envoy::Protobuf::DescriptorPool> descriptor_pool_;

  // A map from {method_name, field_mask} to the respective match tree for request fields.
  StringPairToMatchTreeMap request_field_restrictions_;

  // A map from {method_name, field_mask} to the respective match tree for response fields.
  StringPairToMatchTreeMap response_field_restrictions_;

  // A map from method_name to the respective match tree for method-level restrictions.
  absl::flat_hash_map<std::string, MatchTreeHttpMatchingDataSharedPtr> method_level_restrictions_;

  // A map from message_name to the respective match tree for message-level restrictions.
  absl::flat_hash_map<std::string, MatchTreeHttpMatchingDataSharedPtr> message_level_restrictions_;

  // An instance of `google::grpc::transcoding::TypeHelper` which can be used for type resolution.
  std::unique_ptr<const TypeHelper> type_helper_;

  // A lambda function which resolves type URL string to the corresponding `Protobuf::Type*`.
  // Internally, it uses `type_helper_` for type resolution.
  std::unique_ptr<const TypeFinder> type_finder_;
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
  Matcher::ActionConstSharedPtr createAction(const Protobuf::Message&,
                                             ProtoApiScrubberRemoveFieldAction&,
                                             ProtobufMessage::ValidationVisitor&) override {
    return std::make_shared<RemoveFieldAction>();
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
