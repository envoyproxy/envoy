#pragma once

#include <memory>
#include <string>
#include <utility>

#include "envoy/common/time.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_api_scrubber/v3/matcher_actions.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/http/utility.h"
#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
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

// All stats for the Proto API Scrubber filter. @see stats_macros.h for more details on stats.
#define ALL_PROTO_API_SCRUBBER_STATS(COUNTER, GAUGE, HISTOGRAM)                                    \
  COUNTER(request_scrubbing_failed)                                                                \
  COUNTER(response_scrubbing_failed)                                                               \
  COUNTER(method_blocked)                                                                          \
  COUNTER(request_buffer_conversion_error)                                                         \
  COUNTER(response_buffer_conversion_error)                                                        \
  COUNTER(invalid_method_name)                                                                     \
  COUNTER(total_requests)                                                                          \
  COUNTER(total_requests_checked)                                                                  \
  HISTOGRAM(request_scrubbing_latency, Milliseconds)                                               \
  HISTOGRAM(response_scrubbing_latency, Milliseconds)

struct ProtoApiScrubberStats {
  ALL_PROTO_API_SCRUBBER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                               GENERATE_HISTOGRAM_STRUCT)

  ProtoApiScrubberStats(Envoy::Stats::Scope& scope, absl::string_view prefix)
      : request_scrubbing_failed_(makeCounter(scope, prefix, "request_scrubbing_failed")),
        response_scrubbing_failed_(makeCounter(scope, prefix, "response_scrubbing_failed")),
        method_blocked_(makeCounter(scope, prefix, "method_blocked")),
        request_buffer_conversion_error_(
            makeCounter(scope, prefix, "request_buffer_conversion_error")),
        response_buffer_conversion_error_(
            makeCounter(scope, prefix, "response_buffer_conversion_error")),
        invalid_method_name_(makeCounter(scope, prefix, "invalid_method_name")),
        total_requests_(makeCounter(scope, prefix, "total_requests")),
        total_requests_checked_(makeCounter(scope, prefix, "total_requests_checked")),
        request_scrubbing_latency_(makeHistogram(scope, prefix, "request_scrubbing_latency",
                                                 Stats::Histogram::Unit::Milliseconds)),
        response_scrubbing_latency_(makeHistogram(scope, prefix, "response_scrubbing_latency",
                                                  Stats::Histogram::Unit::Milliseconds)) {}

private:
  static Stats::Counter& makeCounter(Envoy::Stats::Scope& scope, absl::string_view prefix,
                                     absl::string_view name) {
    return scope.counterFromStatName(
        Stats::StatNameManagedStorage(absl::StrCat(prefix, name), scope.symbolTable()).statName());
  }

  static Stats::Histogram& makeHistogram(Envoy::Stats::Scope& scope, absl::string_view prefix,
                                         absl::string_view name, Stats::Histogram::Unit unit) {
    return scope.histogramFromStatName(
        Stats::StatNameManagedStorage(absl::StrCat(prefix, name), scope.symbolTable()).statName(),
        unit);
  }
};

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
   * Returns the match tree associated with a specific field within a message type.
   * This allows defining restrictions that apply whenever a specific message type is encountered,
   * regardless of where it appears (e.g. inside an Any field).
   *
   * @param message_name The fully qualified message name (e.g., "package.MyMessage").
   * @param field_name The name of the field within that message.
   * @return A MatchTreeHttpMatchingDataSharedPtr if a restriction is configured.
   * Returns `nullptr` otherwise.
   */
  virtual MatchTreeHttpMatchingDataSharedPtr
  getMessageFieldMatcher(const std::string& message_name, const std::string& field_name) const;

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
  virtual const TypeFinder& getTypeFinder() const { return *type_finder_; };

  /**
   * Returns the parent Type for a given Field pointer.
   * This map is pre-computed during initialization to allow recovering context when
   * traversing dynamic types (e.g., Any).
   *
   * @param field The field pointer to look up.
   * @return The parent Type pointer, or nullptr if not found.
   */
  virtual const Envoy::Protobuf::Type* getParentType(const Envoy::Protobuf::Field* field) const;

  // Returns the request type of the method.
  virtual absl::StatusOr<const Protobuf::Type*>
  getRequestType(const std::string& method_name) const;

  // Returns the response type of the method.
  virtual absl::StatusOr<const Protobuf::Type*>
  getResponseType(const std::string& method_name) const;

  // Returns method descriptor by looking up the `descriptor_pool_`.
  // If the method doesn't exist in the `descriptor_pool`, it returns absl::InvalidArgument error.
  virtual absl::StatusOr<const MethodDescriptor*>
  getMethodDescriptor(const std::string& method_name) const;

  FilteringMode filteringMode() const { return filtering_mode_; }

  // Returns the filter statistics helper.
  const ProtoApiScrubberStats& stats() const { return stats_; }

  // Returns the time source used for latency measurements.
  TimeSource& timeSource() const { return time_source_; }

protected:
  // Protected constructor to make sure that this class is used in a factory fashion using the
  // public `create` method.
  ProtoApiScrubberFilterConfig(ProtoApiScrubberStats stats, TimeSource& time_source)
      : stats_(stats), time_source_(time_source) {}

private:
  friend class MockProtoApiScrubberFilterConfig;

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

  // Loads and parses the descriptor set from the data source.
  // Returns the parsed FileDescriptorSet and populates the internal descriptor_pool_.
  absl::StatusOr<Envoy::Protobuf::FileDescriptorSet>
  loadDescriptorSet(Api::Api& api, const ::envoy::config::core::v3::DataSource& data_source);

  // Initializes the type utilities (e.g., type helper, type finder, etc.).
  void initializeTypeUtils();

  // Traverses the FileDescriptorSet and pre-computes the Field* -> Parent Type* map.
  // This must be called after initializeTypeUtils().
  void buildFieldParentMap(const Envoy::Protobuf::FileDescriptorSet& descriptor_set);

  // Recursive helper for buildFieldParentMap.
  void populateMapForMessage(const Envoy::Protobuf::DescriptorProto& msg,
                             const std::string& package_prefix);

  /**
   * Helper method to resolve a Protobuf type from its name and populate the type cache.
   *
   * This handles normalizing the fully qualified type name (handling leading dots
   * or prepending the package prefix), constructing the type URL, looking it up
   * via the type finder, and storing the result in the provided cache map.
   * If the type cannot be resolved, an error is logged.
   *
   * @param raw_type_name   The type name as defined in the method descriptor (e.g. "MyMessage" or
   * ".pkg.Msg").
   * @param package_prefix  The package scope of the file (e.g. "my.package.") to use if the type is
   * relative.
   * @param method_key      The unique string key for the method (e.g.
   * "/my.package.Service/Method").
   * @param cache           The specific cache map to populate (request_type_cache_ or
   * response_type_cache_).
   * @param type_category   A label (e.g. "Request" or "Response") used for error logging.
   */
  void resolveAndCacheType(const std::string& raw_type_name, const std::string& package_prefix,
                           const std::string& method_key,
                           absl::flat_hash_map<std::string, const Envoy::Protobuf::Type*>& cache,
                           absl::string_view type_category);

  // Pre-computes the request and response types for all methods in the descriptor set.
  // This allows O(1) access to types during request and response processing.
  void precomputeTypeCache(const Envoy::Protobuf::FileDescriptorSet& descriptor_set);

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

  // A map from {message_name, field_name} to the respective match tree for fields within a message.
  StringPairToMatchTreeMap message_field_restrictions_;

  // A global map used to recover the parent Type context from a Field pointer.
  // This is read-only after initialization.
  absl::flat_hash_map<const Envoy::Protobuf::Field*, const Envoy::Protobuf::Type*>
      field_to_parent_type_map_;

  // An instance of `google::grpc::transcoding::TypeHelper` which can be used for type resolution.
  std::unique_ptr<const TypeHelper> type_helper_;

  // A lambda function which resolves type URL string to the corresponding `Protobuf::Type*`.
  // Internally, it uses `type_helper_` for type resolution.
  std::unique_ptr<const TypeFinder> type_finder_;

  // Caches for request and response types to avoid repeated lookups and string manipulations.
  // These are populated during initialization and read-only afterwards, so no mutex is required.
  absl::flat_hash_map<std::string, const Protobuf::Type*> request_type_cache_;
  absl::flat_hash_map<std::string, const Protobuf::Type*> response_type_cache_;

  // The stats helper used to record filter metrics.
  ProtoApiScrubberStats stats_;

  // The time source used for measuring latency.
  TimeSource& time_source_;
};

// A class to validate the input type specified for the unified matcher in the config.
class MatcherInputValidatorVisitor : public Matcher::MatchTreeValidationVisitor<HttpMatchingData> {
public:
  // Validates whether the input type for the matcher is in the list of supported input types.
  // ProtoApiScrubber filter supports all types of data inputs and hence, it returns
  // `absl::OkStatus()` by default.
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<HttpMatchingData>&,
                                          absl::string_view) override {
    return absl::OkStatus();
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
