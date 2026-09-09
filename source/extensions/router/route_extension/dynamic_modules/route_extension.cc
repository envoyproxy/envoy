#include "source/extensions/router/route_extension/dynamic_modules/route_extension.h"

#include <string>
#include <utility>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"
#include "source/common/config/well_known_names.h"
#include "source/common/http/hash_policy.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/router/retry_policy_impl.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace DynamicModules {
namespace {

using RouteActionOverrideProto =
    envoy::extensions::router::route_extension::dynamic_modules::v3::RouteActionOverride;

absl::StatusOr<RouteActionOverride>
buildRouteActionOverride(const RouteActionOverrideProto& proto_override,
                         Server::Configuration::ServerFactoryContext& context) {
  RouteActionOverride entry;
  if (proto_override.has_retry_policy()) {
    auto policy_or_error = Envoy::Router::RetryPolicyImpl::create(
        proto_override.retry_policy(), context.messageValidationVisitor(), context);
    RETURN_IF_NOT_OK_REF(policy_or_error.status());
    entry.retry_policy = std::move(policy_or_error.value());
  }
  if (proto_override.has_metadata_match()) {
    const auto& filter_metadata = proto_override.metadata_match().filter_metadata();
    const auto filter_it = filter_metadata.find(Envoy::Config::MetadataFilters::get().ENVOY_LB);
    if (filter_it != filter_metadata.end()) {
      entry.metadata_match_criteria =
          std::make_unique<Envoy::Router::MetadataMatchCriteriaImpl>(filter_it->second);
    }
  }
  entry.shadow_policies.reserve(proto_override.request_mirror_policies().size());
  for (const auto& mirror_policy_config : proto_override.request_mirror_policies()) {
    auto policy_or_error = Envoy::Router::ShadowPolicyImpl::create(mirror_policy_config, context);
    RETURN_IF_NOT_OK_REF(policy_or_error.status());
    entry.shadow_policies.push_back(std::move(policy_or_error.value()));
  }
  if (!proto_override.hash_policy().empty()) {
    auto policy_or_error =
        Http::HashPolicyImpl::create(proto_override.hash_policy(), context.regexEngine());
    RETURN_IF_NOT_OK_REF(policy_or_error.status());
    entry.hash_policy = std::move(policy_or_error.value());
  }
  // Validate what was built rather than what was configured. A metadata_match without an envoy.lb
  // entry contributes nothing, so a populated looking configuration can still build an override
  // that replaces no property, which set_route_action_override would then accept as a decision.
  if (entry.retry_policy == nullptr && entry.metadata_match_criteria == nullptr &&
      entry.shadow_policies.empty() && entry.hash_policy == nullptr) {
    return absl::InvalidArgumentError(
        "Route action override must replace at least one route action property");
  }
  return entry;
}

} // namespace

DynamicModuleRouteExtensionConfig::DynamicModuleRouteExtensionConfig(
    absl::string_view extension_name, absl::string_view extension_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    RouteActionOverrideMap route_action_overrides)
    : extension_name_(extension_name), extension_config_(extension_config),
      dynamic_module_(std::move(dynamic_module)),
      route_action_overrides_(std::move(route_action_overrides)) {}

const RouteActionOverride*
DynamicModuleRouteExtensionConfig::routeActionOverride(absl::string_view name) const {
  const auto it = route_action_overrides_.find(name);
  return it != route_action_overrides_.end() ? &it->second : nullptr;
}

absl::Status DynamicModuleRouteExtensionConfig::validateClusters(
    const Upstream::ClusterManager& cluster_manager) const {
  for (const auto& [name, override_entry] : route_action_overrides_) {
    for (const auto& shadow_policy : override_entry.shadow_policies) {
      // A policy that names its cluster through a request header resolves it per request, so only a
      // statically named cluster can be checked here.
      if (!shadow_policy->cluster().empty() &&
          !cluster_manager.hasCluster(shadow_policy->cluster())) {
        return absl::InvalidArgumentError(
            fmt::format("route action override '{}': unknown shadow cluster '{}'", name,
                        shadow_policy->cluster()));
      }
    }
  }
  return absl::OkStatus();
}

DynamicModuleRouteExtensionConfig::~DynamicModuleRouteExtensionConfig() {
  if (in_module_config_ != nullptr) {
    // The destroy hook is resolved before the in-module configuration is created.
    ASSERT(on_config_destroy_ != nullptr);
    on_config_destroy_(in_module_config_);
  }
}

absl::StatusOr<DynamicModuleRouteExtensionConfigSharedPtr>
newDynamicModuleRouteExtensionConfig(const DynamicModuleRouteExtensionProto& proto_config,
                                     Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                     Server::Configuration::ServerFactoryContext& context) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  auto on_config_new = dynamic_module->getFunctionPointer<OnRouteExtensionConfigNewType>(
      "envoy_dynamic_module_on_route_extension_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnRouteExtensionConfigDestroyType>(
      "envoy_dynamic_module_on_route_extension_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_route = dynamic_module->getFunctionPointer<OnRouteExtensionOnRouteType>(
      "envoy_dynamic_module_on_route_extension_on_route");
  RETURN_IF_NOT_OK_REF(on_route.status());

  // Use knownAnyToBytes() to properly handle StringValue/BytesValue/Struct types.
  std::string extension_config;
  if (proto_config.has_extension_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.extension_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    extension_config = std::move(config_or_error.value());
  }

  RouteActionOverrideMap route_action_overrides;
  route_action_overrides.reserve(proto_config.route_action_overrides().size());
  for (const auto& [name, proto_override] : proto_config.route_action_overrides()) {
    auto entry_or_error = buildRouteActionOverride(proto_override, context);
    RETURN_IF_NOT_OK_REF(entry_or_error.status());
    route_action_overrides.emplace(name, std::move(entry_or_error.value()));
  }

  auto config = std::make_shared<DynamicModuleRouteExtensionConfig>(
      proto_config.extension_name(), extension_config, std::move(dynamic_module),
      std::move(route_action_overrides));
  config->on_config_destroy_ = on_config_destroy.value();
  config->on_route_ = on_route.value();

  const envoy_dynamic_module_type_envoy_buffer name_buf = {
      .ptr = config->extension_name_.data(), .length = config->extension_name_.size()};
  const envoy_dynamic_module_type_envoy_buffer config_buf = {
      .ptr = config->extension_config_.data(), .length = config->extension_config_.size()};
  config->in_module_config_ =
      (*on_config_new.value())(static_cast<void*>(config.get()), name_buf, config_buf);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module route extension config");
  }
  return config;
}

Envoy::Router::RouteConstSharedPtr DynamicModuleRouteExtension::onRoute(
    Envoy::Router::RouteConstSharedPtr route, const Http::RequestHeaderMap& headers,
    const StreamInfo::StreamInfo& stream_info, uint64_t random_value) const {
  RouteExtensionContext context{*config_, headers, stream_info, random_value, {}};
  const envoy_dynamic_module_type_route_extension_decision decision =
      config_->on_route_(config_->in_module_config_, static_cast<void*>(&context));

  switch (decision) {
  case envoy_dynamic_module_type_route_extension_decision_Drop:
    return nullptr;
  case envoy_dynamic_module_type_route_extension_decision_Override: {
    // A route without a route entry, such as a direct response or a redirect, cannot have its route
    // action overridden, so keep it as is.
    if (route == nullptr || route->routeEntry() == nullptr) {
      return route;
    }
    // An override that replaced no property keeps the matched route, so avoid wrapping it.
    if (context.overrides.cluster_name.empty() &&
        context.overrides.route_action_override == nullptr) {
      return route;
    }
    return std::make_shared<DynamicModuleRouteEntry>(std::move(route), config_,
                                                     std::move(context.overrides.cluster_name),
                                                     context.overrides.route_action_override);
  }
  case envoy_dynamic_module_type_route_extension_decision_Keep:
    break;
  }
  return route;
}

} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy
