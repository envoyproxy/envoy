#include "source/extensions/router/cluster_specifiers/dynamic_modules/cluster_specifier.h"

#include <string>
#include <utility>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/config/well_known_names.h"
#include "source/common/http/hash_policy.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/router/retry_policy_impl.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace DynamicModules {
namespace {

using RouteActionOverrideProto =
    envoy::extensions::router::cluster_specifiers::dynamic_modules::v3::RouteActionOverride;

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

DynamicModuleClusterSpecifierConfig::DynamicModuleClusterSpecifierConfig(
    absl::string_view specifier_name, absl::string_view specifier_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    RouteActionOverrideMap route_action_overrides, Upstream::ClusterManager& cluster_manager,
    Stats::Scope& stats_scope, absl::string_view metrics_namespace)
    : stats_scope_(stats_scope.createScope(absl::StrCat(metrics_namespace, "."))),
      stat_name_pool_(stats_scope_->symbolTable()), specifier_name_(specifier_name),
      specifier_config_(specifier_config), dynamic_module_(std::move(dynamic_module)),
      cluster_manager_(cluster_manager),
      route_action_overrides_(std::move(route_action_overrides)) {}

DynamicModuleClusterSpecifierConfig::~DynamicModuleClusterSpecifierConfig() {
  if (in_module_config_ != nullptr) {
    // The destroy hook is resolved before the in-module configuration is created.
    ASSERT(on_config_destroy_ != nullptr);
    on_config_destroy_(in_module_config_);
  }
}

const RouteActionOverride*
DynamicModuleClusterSpecifierConfig::routeActionOverride(absl::string_view name) const {
  const auto it = route_action_overrides_.find(name);
  return it != route_action_overrides_.end() ? &it->second : nullptr;
}

absl::Status DynamicModuleClusterSpecifierConfig::validateClusters(
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

absl::StatusOr<DynamicModuleClusterSpecifierConfigSharedPtr>
newDynamicModuleClusterSpecifierConfig(const DynamicModuleClusterSpecifierProto& proto_config,
                                       Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                       Server::Configuration::ServerFactoryContext& context) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  auto on_config_new = dynamic_module->getFunctionPointer<OnClusterSpecifierConfigNewType>(
      "envoy_dynamic_module_on_cluster_specifier_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnClusterSpecifierConfigDestroyType>(
      "envoy_dynamic_module_on_cluster_specifier_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_select = dynamic_module->getFunctionPointer<OnClusterSpecifierSelectType>(
      "envoy_dynamic_module_on_cluster_specifier_select");
  RETURN_IF_NOT_OK_REF(on_select.status());

  // Use knownAnyToBytes() to properly handle StringValue/BytesValue/Struct types.
  std::string specifier_config;
  if (proto_config.has_specifier_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.specifier_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    specifier_config = std::move(config_or_error.value());
  }

  RouteActionOverrideMap route_action_overrides;
  route_action_overrides.reserve(proto_config.route_action_overrides().size());
  for (const auto& [name, proto_override] : proto_config.route_action_overrides()) {
    auto entry_or_error = buildRouteActionOverride(proto_override, context);
    RETURN_IF_NOT_OK_REF(entry_or_error.status());
    route_action_overrides.emplace(name, std::move(entry_or_error.value()));
  }

  const std::string metrics_namespace =
      proto_config.dynamic_module_config().metrics_namespace().empty()
          ? std::string(DefaultMetricsNamespace)
          : proto_config.dynamic_module_config().metrics_namespace();
  // When the runtime guard is enabled, register the metrics namespace as a custom stat namespace.
  // This causes the namespace prefix to be stripped from prometheus output and no envoy_ prefix
  // is added. This is the legacy behavior for backward compatibility.
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
    context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
  }

  auto config = std::make_shared<DynamicModuleClusterSpecifierConfig>(
      proto_config.specifier_name(), specifier_config, std::move(dynamic_module),
      std::move(route_action_overrides), context.clusterManager(), context.serverScope(),
      metrics_namespace);
  config->on_config_destroy_ = on_config_destroy.value();
  config->on_select_ = on_select.value();

  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = config->specifier_name_.data(),
                                                     .length = config->specifier_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buf = {.ptr = config->specifier_config_.data(),
                                                       .length = config->specifier_config_.size()};
  config->in_module_config_ =
      (*on_config_new.value())(static_cast<void*>(config.get()), name_buf, config_buf);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to initialize dynamic module cluster specifier config");
  }
  config->stat_creation_frozen_.store(true, std::memory_order_release);
  return config;
}

void DynamicModuleRouteEntry::refreshRouteCluster(const Http::RequestHeaderMap& headers,
                                                  const StreamInfo::StreamInfo& stream_info) const {
  ClusterSpecifierContext context{*config_, headers, stream_info, routeName(), random_value_, {}};
  if (!config_->on_select_(config_->in_module_config_, static_cast<void*>(&context))) {
    ENVOY_LOG(debug, "dynamic module reached no cluster selection, keeping the previous one");
    return;
  }
  // The new selection replaces the previous one, so properties the module left unset on this call
  // fall back to the matched route.
  selection_ = std::move(context.selection);
  ENVOY_LOG(debug, "dynamic module selected cluster '{}'", clusterName());
}

Envoy::Router::RouteConstSharedPtr DynamicModuleClusterSpecifierPlugin::route(
    Envoy::Router::RouteEntryAndRouteConstSharedPtr parent, const Http::RequestHeaderMap& headers,
    const StreamInfo::StreamInfo& stream_info, uint64_t random_value) const {
  // The delegating route entry is returned even when the module makes no decision so that a later
  // refresh still reaches the module.
  auto route = std::make_shared<DynamicModuleRouteEntry>(std::move(parent), config_, random_value);
  route->refreshRouteCluster(headers, stream_info);
  return route;
}

} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy
