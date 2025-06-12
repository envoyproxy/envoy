#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include <algorithm>

#include "envoy/extensions/filters/http/proto_api_scrubber/v3/matcher_actions.pb.h"
#include "envoy/matcher/matcher.h"

#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {

static constexpr absl::string_view kConfigInitializationError =
    "Error encountered during config initialization.";

// Returns whether the fully qualified `api_name` is valid or not.
// Checks for separator '.' in the name and verifies each substring between these separators are
// non-empty. Note that it does not verify whether the API actually exists or not.
bool isApiNameValid(absl::string_view api_name) {
  const std::vector<absl::string_view> api_name_parts = absl::StrSplit(api_name, '.');
  if (api_name_parts.size() <= 1) {
    return false;
  }

  // Returns true if all of the api_name_parts are non-empty, otherwise returns false.
  return !std::any_of(api_name_parts.cbegin(), api_name_parts.cend(),
                      [](const absl::string_view s) { return s.empty(); });
}

// Creates and returns a CEL matcher.
MatchTreeHttpMatchingDataSharedPtr
getCelMatcher(Envoy::Server::Configuration::ServerFactoryContext& server_factory_context,
              const xds::type::matcher::v3::Matcher& matcher) {
  ProtoApiScrubberRemoveFieldAction remove_field_action;
  MatcherInputValidatorVisitor validation_visitor;
  Matcher::MatchTreeFactory<HttpMatchingData, ProtoApiScrubberRemoveFieldAction> matcher_factory(
      remove_field_action, server_factory_context, validation_visitor);
  Matcher::MatchTreeFactoryCb<HttpMatchingData> match_tree_factory_cb =
      matcher_factory.create(matcher);

  // Call the match tree factory callback to return the underlying match_tree.
  return match_tree_factory_cb();
}

} // namespace

absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>>
ProtoApiScrubberFilterConfig::create(const ProtoApiScrubberConfig& proto_config,
                                     Server::Configuration::FactoryContext& context) {
  std::shared_ptr<ProtoApiScrubberFilterConfig> filter_config_ptr =
      std::shared_ptr<ProtoApiScrubberFilterConfig>(new ProtoApiScrubberFilterConfig());
  RETURN_IF_ERROR(filter_config_ptr->initialize(proto_config, context));
  return filter_config_ptr;
}

absl::Status
ProtoApiScrubberFilterConfig::initialize(const ProtoApiScrubberConfig& proto_config,
                                         Server::Configuration::FactoryContext& context) {
  ENVOY_LOG(trace, "Initializing filter config from the proto config: {}",
            proto_config.DebugString());

  FilteringMode filtering_mode = proto_config.filtering_mode();
  RETURN_IF_ERROR(validateFilteringMode(filtering_mode));
  filtering_mode_ = filtering_mode;

  for (const auto& method_restriction : proto_config.restrictions().method_restrictions()) {
    std::string method_name = method_restriction.first;
    RETURN_IF_ERROR(validateMethodName(method_name));
    RETURN_IF_ERROR(initializeMethodRestrictions(
        method_name, request_field_restrictions_,
        method_restriction.second.request_field_restrictions(), context));
    RETURN_IF_ERROR(initializeMethodRestrictions(
        method_name, response_field_restrictions_,
        method_restriction.second.response_field_restrictions(), context));
  }

  ENVOY_LOG(trace, "Filter config initialized successfully.");
  return absl::OkStatus();
}

absl::Status ProtoApiScrubberFilterConfig::validateFilteringMode(FilteringMode filtering_mode) {
  switch (filtering_mode) {
  case FilteringMode::ProtoApiScrubberConfig_FilteringMode_OVERRIDE:
    return absl::OkStatus();
  default:
    return absl::InvalidArgumentError(
        fmt::format("{} Unsupported 'filtering_mode': {}.", kConfigInitializationError,
                    envoy::extensions::filters::http::proto_api_scrubber::v3::
                        ProtoApiScrubberConfig_FilteringMode_Name(filtering_mode)));
  }
}

absl::Status ProtoApiScrubberFilterConfig::validateMethodName(absl::string_view method_name) {
  if (method_name.empty()) {
    return absl::InvalidArgumentError(
        fmt::format("{} Invalid method name: '{}'. Method name is empty.",
                    kConfigInitializationError, method_name));
  }

  if (absl::StrContains(method_name, '*')) {
    return absl::InvalidArgumentError(fmt::format(
        "{} Invalid method name: '{}'. Method name contains '*' which is not supported.",
        kConfigInitializationError, method_name));
  }

  const std::vector<absl::string_view> method_name_parts = absl::StrSplit(method_name, '/');
  if (method_name_parts.size() != 3 || !method_name_parts[0].empty() ||
      method_name_parts[1].empty() || !isApiNameValid(method_name_parts[1]) ||
      method_name_parts[2].empty()) {
    return absl::InvalidArgumentError(
        fmt::format("{} Invalid method name: '{}'. Method name should follow the gRPC format "
                    "('/package.ServiceName/MethodName').",
                    kConfigInitializationError, method_name));
  }

  return absl::OkStatus();
}

absl::Status ProtoApiScrubberFilterConfig::validateFieldMask(absl::string_view field_mask) {
  if (field_mask.empty()) {
    return absl::InvalidArgumentError(
        fmt::format("{} Invalid field mask: '{}'. Field mask is empty.", kConfigInitializationError,
                    field_mask));
  }

  if (absl::StrContains(field_mask, '*')) {
    return absl::InvalidArgumentError(
        fmt::format("{} Invalid field mask: '{}'. Field mask contains '*' which is not supported.",
                    kConfigInitializationError, field_mask));
  }

  return absl::OkStatus();
}

absl::Status ProtoApiScrubberFilterConfig::initializeMethodRestrictions(
    absl::string_view method_name, StringPairToMatchTreeMap& field_restrictions,
    const Map<std::string, RestrictionConfig>& restrictions,
    Envoy::Server::Configuration::FactoryContext& context) {
  for (const auto& restriction : restrictions) {
    absl::string_view field_mask = restriction.first;
    RETURN_IF_ERROR(validateFieldMask(field_mask));
    field_restrictions[std::make_pair(std::string(method_name), std::string(field_mask))] =
        getCelMatcher(context.serverFactoryContext(), restriction.second.matcher());
  }

  return absl::OkStatus();
}

MatchTreeHttpMatchingDataSharedPtr
ProtoApiScrubberFilterConfig::getRequestFieldMatcher(const std::string& method_name,
                                                     const std::string& field_mask) const {
  if (auto it = request_field_restrictions_.find(std::make_pair(method_name, field_mask));
      it != request_field_restrictions_.end()) {
    return it->second;
  }

  return nullptr;
}

MatchTreeHttpMatchingDataSharedPtr
ProtoApiScrubberFilterConfig::getResponseFieldMatcher(const std::string& method_name,
                                                      const std::string& field_mask) const {
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
