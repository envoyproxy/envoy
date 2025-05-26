#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

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

ProtoApiScrubberFilterConfig::ProtoApiScrubberFilterConfig() {}

absl::StatusOr<std::shared_ptr<ProtoApiScrubberFilterConfig>>
ProtoApiScrubberFilterConfig::create(const ProtoApiScrubberConfig& proto_config,
                                     Server::Configuration::FactoryContext& context) {
  auto filter_config_ptr =
      std::shared_ptr<ProtoApiScrubberFilterConfig>(new ProtoApiScrubberFilterConfig());
  if (absl::Status status = filter_config_ptr->initialize(proto_config, context); !status.ok()) {
    return status;
  }

  return filter_config_ptr;
}

absl::Status
ProtoApiScrubberFilterConfig::initialize(const ProtoApiScrubberConfig& proto_config,
                                         Server::Configuration::FactoryContext& context) {
  ENVOY_LOG(debug, "Initializing filter config from the proto config: {}",
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

  ENVOY_LOG(debug, "Filter config initialized successfully.");
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

  std::vector<absl::string_view> method_name_parts = absl::StrSplit(method_name, '/');
  if (method_name_parts.size() != 3 || !method_name_parts[0].empty() ||
      method_name_parts[1].empty() || !absl::StrContains(method_name_parts[1], '.') ||
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
    std::string method_name, StringPairToMatchTreeMap& field_restrictions,
    const Map<std::string, RestrictionConfig> restrictions,
    Envoy::Server::Configuration::FactoryContext& context) {
  for (const auto& restriction : restrictions) {
    std::string field_mask = restriction.first;
    RETURN_IF_ERROR(validateFieldMask(field_mask));
    ProtoApiScrubberRemoveFieldAction remove_field_action;
    ActionValidatorVisitor validation_visitor;
    Matcher::MatchTreeFactory<HttpMatchingData, ProtoApiScrubberRemoveFieldAction> matcher_factory(
        remove_field_action, context.serverFactoryContext(), validation_visitor);

    absl::optional<Matcher::MatchTreeFactoryCb<HttpMatchingData>> factory_cb =
        matcher_factory.create(restriction.second.matcher());
    if (factory_cb.has_value()) {
      field_restrictions[std::make_pair(method_name, field_mask)] = factory_cb.value()();
    } else {
      return absl::InvalidArgumentError(fmt::format(
          "{} Failed to initialize matcher factory callback for method {} and field mask {}.",
          kConfigInitializationError, method_name, field_mask));
    }
  }

  return absl::OkStatus();
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
