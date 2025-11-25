#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/filters/http/proto_api_scrubber/v3/matcher_actions.pb.h"
#include "envoy/matcher/matcher.h"

#include "source/common/grpc/common.h"
#include "source/common/matcher/matcher.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "fmt/core.h"
#include "grpc_transcoding/type_helper.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {

using google::grpc::transcoding::TypeHelper;
using Protobuf::MethodDescriptor;

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

  // Initialize filtering mode.
  FilteringMode filtering_mode = proto_config.filtering_mode();
  RETURN_IF_ERROR(validateFilteringMode(filtering_mode));
  filtering_mode_ = filtering_mode;

  // Initialize proto descriptor pool.
  absl::Status descriptor_pool_init_status = initializeDescriptorPool(
      context.serverFactoryContext().api(), proto_config.descriptor_set().data_source());
  RETURN_IF_ERROR(descriptor_pool_init_status);

  if (proto_config.has_restrictions()) {
    for (const auto& method_restriction_pair : proto_config.restrictions().method_restrictions()) {
      const std::string& method_name = method_restriction_pair.first;
      const auto& method_config = method_restriction_pair.second;
      RETURN_IF_ERROR(validateMethodName(method_name));
      RETURN_IF_ERROR(initializeMethodFieldRestrictions(method_name, request_field_restrictions_,
                                                        method_config.request_field_restrictions(),
                                                        context));
      RETURN_IF_ERROR(initializeMethodFieldRestrictions(method_name, response_field_restrictions_,
                                                        method_config.response_field_restrictions(),
                                                        context));
      RETURN_IF_ERROR(initializeMethodLevelRestrictions(method_name, method_config, context));
    }

    RETURN_IF_ERROR(
        initializeMessageRestrictions(proto_config.restrictions().message_restrictions(), context));
  }

  initializeTypeUtils();

  ENVOY_LOG(trace, "Filter config initialized successfully.");
  return absl::OkStatus();
}

absl::Status ProtoApiScrubberFilterConfig::initializeMethodLevelRestrictions(
    absl::string_view method_name, const MethodRestrictions& method_config,
    Envoy::Server::Configuration::FactoryContext& context) {
  if (method_config.has_method_restriction()) {
    method_level_restrictions_[method_name] =
        getCelMatcher(context.serverFactoryContext(), method_config.method_restriction().matcher());
  }
  return absl::OkStatus();
}

absl::Status ProtoApiScrubberFilterConfig::initializeMessageRestrictions(
    const Map<std::string, MessageRestrictions>& message_configs,
    Envoy::Server::Configuration::FactoryContext& context) {
  for (const auto& pair : message_configs) {
    const std::string& message_name = pair.first;
    const auto& message_config = pair.second;
    absl::Status name_status = validateMessageName(message_name);
    if (!name_status.ok()) {
      return name_status;
    }
    if (message_config.has_config()) {
      message_level_restrictions_[message_name] =
          getCelMatcher(context.serverFactoryContext(), message_config.config().matcher());
    }
  }
  return absl::OkStatus();
}

absl::Status ProtoApiScrubberFilterConfig::initializeDescriptorPool(
    Api::Api& api, const ::envoy::config::core::v3::DataSource& data_source) {
  Envoy::Protobuf::FileDescriptorSet descriptor_set;

  switch (data_source.specifier_case()) {
  case envoy::config::core::v3::DataSource::SpecifierCase::kFilename: {
    auto file_or_error = api.fileSystem().fileReadToEnd(data_source.filename());
    if (!file_or_error.status().ok()) {
      return absl::InvalidArgumentError(fmt::format(
          "{} Unable to read from file `{}`", kConfigInitializationError, data_source.filename()));
    }

    if (!descriptor_set.ParseFromString(file_or_error.value())) {
      return absl::InvalidArgumentError(
          fmt::format("{} Unable to parse proto descriptor from file `{}`",
                      kConfigInitializationError, data_source.filename()));
    }
    break;
  }
  case envoy::config::core::v3::DataSource::SpecifierCase::kInlineBytes: {
    if (!descriptor_set.ParseFromString(data_source.inline_bytes())) {
      return absl::InvalidArgumentError(
          fmt::format("{} Unable to parse proto descriptor from inline bytes `{}`",
                      kConfigInitializationError, data_source.inline_bytes()));
    }
    break;
  }
  default:
    return absl::InvalidArgumentError(
        fmt::format("{} Unsupported DataSource case `{}` for configuring `descriptor_set`",
                    kConfigInitializationError, static_cast<int>(data_source.specifier_case())));
  }

  auto pool = std::make_unique<Envoy::Protobuf::DescriptorPool>();
  for (const auto& file : descriptor_set.file()) {
    if (pool->BuildFile(file) == nullptr) {
      return absl::InvalidArgumentError(
          fmt::format("{} Error occurred in file `{}` while trying to build proto descriptors.",
                      kConfigInitializationError, file.name()));
    }
  }

  descriptor_pool_ = std::move(pool);
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

absl::Status ProtoApiScrubberFilterConfig::validateMessageName(absl::string_view message_name) {
  if (message_name.empty()) {
    return absl::InvalidArgumentError(
        fmt::format("{} Invalid message name: '{}'. Message name is empty.",
                    kConfigInitializationError, message_name));
  }
  if (!isApiNameValid(message_name)) {
    return absl::InvalidArgumentError(
        fmt::format("{} Invalid message name: '{}'. Message name should be fully qualified (e.g., "
                    "package.Message).",
                    kConfigInitializationError, message_name));
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

absl::Status ProtoApiScrubberFilterConfig::initializeMethodFieldRestrictions(
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

MatchTreeHttpMatchingDataSharedPtr
ProtoApiScrubberFilterConfig::getMethodMatcher(const std::string& method_name) const {
  if (auto it = method_level_restrictions_.find(method_name);
      it != method_level_restrictions_.end()) {
    return it->second;
  }
  return nullptr;
}

MatchTreeHttpMatchingDataSharedPtr
ProtoApiScrubberFilterConfig::getMessageMatcher(const std::string& message_name) const {
  if (auto it = message_level_restrictions_.find(message_name);
      it != message_level_restrictions_.end()) {
    return it->second;
  }
  return nullptr;
}

void ProtoApiScrubberFilterConfig::initializeTypeUtils() {
  type_helper_ =
      std::make_unique<const TypeHelper>(Envoy::Protobuf::util::NewTypeResolverForDescriptorPool(
          Envoy::Grpc::Common::typeUrlPrefix(), descriptor_pool_.get()));

  type_finder_ = std::make_unique<const TypeFinder>(
      [this](absl::string_view type_url) -> const ::Envoy::Protobuf::Type* {
        return type_helper_->Info()->GetTypeByTypeUrl(type_url);
      });
}

absl::StatusOr<const MethodDescriptor*>
ProtoApiScrubberFilterConfig::getMethodDescriptor(const std::string& method_name) const {
  // Covert grpc method name from `/package.service/method` format to `package.service.method` as
  // the method `FindMethodByName` expects the method name to be in the latter format.
  std::string dot_separated_method_name =
      absl::StrReplaceAll(absl::StripPrefix(method_name, "/"), {{"/", "."}});
  const MethodDescriptor* method = descriptor_pool_->FindMethodByName(dot_separated_method_name);
  if (method == nullptr) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Unable to find method `%s` in the descriptor pool configured for this filter.",
        dot_separated_method_name));
  }

  return method;
}

absl::StatusOr<const Protobuf::Type*>
ProtoApiScrubberFilterConfig::getRequestType(const std::string& method_name) const {
  absl::StatusOr<const MethodDescriptor*> method_or_status = getMethodDescriptor(method_name);
  RETURN_IF_NOT_OK(method_or_status.status());

  std::string request_type_url = absl::StrCat(Envoy::Grpc::Common::typeUrlPrefix(), "/",
                                              method_or_status.value()->input_type()->full_name());
  const Protobuf::Type* request_type = (*type_finder_)(request_type_url);
  return request_type;
}

absl::StatusOr<const Protobuf::Type*>
ProtoApiScrubberFilterConfig::getResponseType(const std::string& method_name) const {
  absl::StatusOr<const MethodDescriptor*> method_or_status = getMethodDescriptor(method_name);
  RETURN_IF_NOT_OK(method_or_status.status());

  std::string response_type_url =
      absl::StrCat(Envoy::Grpc::Common::typeUrlPrefix(), "/",
                   method_or_status.value()->output_type()->full_name());
  const Protobuf::Type* response_type = (*type_finder_)(response_type_url);
  return response_type;
}

REGISTER_FACTORY(RemoveFilterActionFactory,
                 Matcher::ActionFactory<ProtoApiScrubberRemoveFieldAction>);

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
