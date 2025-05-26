#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/filters/http/proto_api_scrubber/v3/matcher_actions.pb.h"
#include "envoy/matcher/matcher.h"

#include "source/common/matcher/matcher.h"

#include "xds/type/matcher/v3/http_inputs.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {
using Protobuf::Map;
using ProtoApiScrubberRemoveFieldAction =
    ::envoy::extensions::filters::http::proto_api_scrubber::v3::RemoveFieldAction;
using ::envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig;
using ::envoy::extensions::filters::http::proto_api_scrubber::v3::RestrictionConfig;
using ::envoy::extensions::filters::http::proto_api_scrubber::v3::Restrictions;
using Http::HttpMatchingData;
using xds::type::matcher::v3::HttpAttributesCelMatchInput;
using MatchTreeHttpCelInputSharedPtr = Matcher::MatchTreeSharedPtr<HttpMatchingData>;
using FilteringMode = ProtoApiScrubberConfig::FilteringMode;

using StringPairToMatchTreeMap =
    absl::flat_hash_map<std::pair<std::string, std::string>, MatchTreeHttpMatchingDataSharedPtr>;

static constexpr absl::string_view kConfigInitializationError =
    "Error encountered during config initialization.";

} // namespace

ProtoApiScrubberFilterConfig::ProtoApiScrubberFilterConfig(
    const ProtoApiScrubberConfig& proto_config,
    Envoy::Server::Configuration::FactoryContext& context) {
  // tODo: UT on what happens if none is provided. Also add UTs, just for filtering mode.
  ENVOY_LOG(debug, "Initializing filter config from the proto config: {}",
            proto_config.DebugString());
  FilteringMode filtering_mode = proto_config.filtering_mode();
  if (!isFilteringModeSupported(filtering_mode)) {
    std::string error_message =
        fmt::format("{} Unsupported 'filtering_mode': {}.", kConfigInitializationError,
                    envoy::extensions::filters::http::proto_api_scrubber::v3::
                        ProtoApiScrubberConfig_FilteringMode_Name(filtering_mode));
    ENVOY_LOG(error, error_message);
    throw EnvoyException(error_message);
  }

  filtering_mode_ = filtering_mode;
  // TODO: UTs if restriction is empty.
  // TODO: UTs if restriction.method_restrictions.empty (Maybe proto creates a default instance)
  for (const auto& method_restriction : proto_config.restrictions().method_restrictions()) {
    std::string method_name = method_restriction.first;
    // TODO: UT for method name validations.
    if (absl::optional<std::string> validation_error = validateMethodName(method_name)) {
      std::string error_message =
          fmt::format("{} Invalid method name: {}. {}", kConfigInitializationError, method_name,
                      *validation_error);
      ENVOY_LOG(error, error_message);
      throw EnvoyException(error_message);
    }

    // TODO: UT to check what happens if method_restriction.second is empty and when
    // method_restriction.second.request_field_restrictions is empty. Initialize method's request
    // field restrictions.
    initializeMethodRestrictions(method_name, request_field_restrictions_,
                                 method_restriction.second.request_field_restrictions(), context);

    // Initialize method's response field restrictions.
    initializeMethodRestrictions(method_name, response_field_restrictions_,
                                 method_restriction.second.response_field_restrictions(), context);
  }

  ENVOY_LOG(debug, "Filter config initialized successfully.");
}

bool ProtoApiScrubberFilterConfig::isFilteringModeSupported(FilteringMode filtering_mode) {
  switch (filtering_mode) {
  case FilteringMode::ProtoApiScrubberConfig_FilteringMode_OVERRIDE:
    return true;
  default:
    return false;
  }
}

absl::optional<std::string>
ProtoApiScrubberFilterConfig::validateMethodName(absl::string_view method_name) {
  std::string error_message;
  if (method_name.empty()) {
    return "Method name is empty";
  }

  if (absl::StrContains(method_name, '*')) {
    return "Method name contains '*' which is not supported.";
  }

  std::vector<absl::string_view> method_name_parts = absl::StrSplit(method_name, '/');
  if (method_name_parts.size() != 3 || !method_name_parts[0].empty() ||
      method_name_parts[1].empty() || !absl::StrContains(method_name_parts[1], '.') ||
      method_name_parts[2].empty()) {
    return "Method name should follow the gRPC format ('/package.ServiceName/MethodName').";
  }

  return std::nullopt;
}

absl::optional<std::string>
ProtoApiScrubberFilterConfig::validateFieldMask(absl::string_view field_mask) {
  std::string error_message;
  if (field_mask.empty()) {
    return "Field mask is empty.";
  }

  if (absl::StrContains(field_mask, '*')) {
    return "Field mask contains '*' which is not supported.";
  }

  return std::nullopt;
}

void ProtoApiScrubberFilterConfig::initializeMethodRestrictions(
    std::string method_name, StringPairToMatchTreeMap& field_restrictions,
    const Map<std::string, RestrictionConfig> restrictions,
    Envoy::Server::Configuration::FactoryContext& context) {
  for (const auto& restriction : restrictions) {
    std::string field_mask = restriction.first;
    if (absl::optional<std::string> validation_error = validateFieldMask(field_mask)) {
      std::string error_message =
          fmt::format("{} Invalid field name: {} for method {}. {}", kConfigInitializationError,
                      field_mask, method_name, *validation_error);
      ENVOY_LOG(error, error_message);
      throw EnvoyException(error_message);
    }

    // TODO: UT to check whether it returns nullptr or a default object if nothing is provided in
    // restriction.second if (!restriction.second || !restriction.second.matcher()) {
    //  continue;
    // }

    ProtoApiScrubberRemoveFieldAction remove_field_action;
    ActionValidatorVisitor validation_visitor;
    Matcher::MatchTreeFactory<HttpMatchingData, ProtoApiScrubberRemoveFieldAction> matcher_factory(
        remove_field_action, context.serverFactoryContext(), validation_visitor);

    absl::optional<Matcher::MatchTreeFactoryCb<HttpMatchingData>> factory_cb =
        matcher_factory.create(restriction.second.matcher());
    if (factory_cb.has_value()) {
      // TODO: Move to single statement
      auto fcbv = factory_cb.value();
      field_restrictions[std::make_pair(method_name, field_mask)] = fcbv();
    } else {
      std::string error_message = fmt::format(
          "{} Failed to initialize matcher factory callback for method {} and field mask {}",
          kConfigInitializationError, method_name, field_mask);
      ENVOY_LOG(error, error_message);
      throw EnvoyException(error_message);
    }
  }
}

MatchTreeHttpCelInputSharedPtr
ProtoApiScrubberFilterConfig::getRequestFieldMatcher(std::string method_name,
                                                     std::string field_mask) const {
  if (auto it = request_field_restrictions_.find(std::make_pair(method_name, field_mask));
      it != request_field_restrictions_.end()) {
    return it->second;
  }

  return nullptr;
}

MatchTreeHttpCelInputSharedPtr
ProtoApiScrubberFilterConfig::getResponseFieldMatcher(std::string method_name,
                                                      std::string field_mask) const {
  if (auto it = response_field_restrictions_.find(std::make_pair(method_name, field_mask));
      it != response_field_restrictions_.end()) {
    return it->second;
  }

  return nullptr;
}

REGISTER_FACTORY(RemoveFilterActionFactory,
                 Matcher::ActionFactory<ProtoApiScrubberRemoveFieldAction>);

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
