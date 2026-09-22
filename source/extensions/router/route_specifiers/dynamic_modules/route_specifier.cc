#include "source/extensions/router/route_specifiers/dynamic_modules/route_specifier.h"

#include <string>
#include <utility>

#include "envoy/common/exception.h"
#include "envoy/ratelimit/ratelimit.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"
#include "source/common/config/well_known_names.h"
#include "source/common/http/hash_policy.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/router/retry_policy_impl.h"
#include "source/common/router/router_ratelimit.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/utility.h"

namespace Envoy {
namespace Extensions {
namespace RouteSpecifiers {
namespace DynamicModules {
namespace {

using Failure = envoy_dynamic_module_type_route_specifier_failure;
using ModuleDecision = envoy_dynamic_module_type_route_specifier_decision;
using RouteKind = envoy_dynamic_module_type_route_specifier_route_kind;
using RouteActionOverrideProto =
    envoy::extensions::router::route_specifiers::dynamic_modules::v3::RouteActionOverride;

// The names of the compared properties, indexed by CompareField, used for the mismatch counters.
// The empty first entry keeps the index and the enum value aligned.
constexpr absl::string_view CompareFieldNames[] = {
    "",
    "route_kind",
    "cluster_name",
    "timeout",
    "idle_timeout",
    "max_stream_duration",
    "priority",
    "request_body_buffer_limit",
    "cluster_not_found_response_code",
    "retry_policy",
    "hedge_policy",
    "metadata_match",
    "hash_policy",
    "request_mirror_policies",
    "request_path",
    "request_authority",
    "request_headers",
    "response_headers",
    "filter_disabled",
    "response_code",
    "redirect_location",
    "virtual_host_name",
    "route_metadata",
    "direct_response_body",
    "route_name",
    "rate_limit_policy",
    "cors",
    "tracing",
};

constexpr size_t CompareFieldCount = ABSL_ARRAYSIZE(CompareFieldNames);
static_assert(CompareFieldCount <= 64, "compare fields are reported to the module as a bit set");
static_assert(
    CompareFieldCount ==
        ShadowModeProto::
            CompareField_ARRAYSIZE, // NOLINT(readability-static-accessed-through-instance)
    "the compared properties must match the ones of the configuration");

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
  if (proto_override.has_hedge_policy()) {
    entry.hedge_policy =
        std::make_unique<Envoy::Router::HedgePolicyImpl>(proto_override.hedge_policy());
  }
  if (!proto_override.rate_limits().empty() || proto_override.has_cors()) {
    // RateLimitPolicyImpl and CorsPolicyImpl build extension backed matchers that throw on a
    // rejected input, so a build failure is turned into a configuration error.
    TRY_NEEDS_AUDIT {
      if (!proto_override.rate_limits().empty()) {
        absl::Status creation_status = absl::OkStatus();
        entry.rate_limit_policy = std::make_unique<Envoy::Router::RateLimitPolicyImpl>(
            proto_override.rate_limits(), context, creation_status);
        RETURN_IF_NOT_OK(creation_status);
      }
      if (proto_override.has_cors()) {
        entry.cors_policy =
            std::make_unique<Envoy::Router::CorsPolicyImpl>(proto_override.cors(), context);
      }
    }
    END_TRY
    CATCH(const EnvoyException& e, { return absl::InvalidArgumentError(e.what()); });
  }
  // Validate what was built rather than what was configured. A metadata_match without an envoy.lb
  // entry contributes nothing, so a populated looking configuration can still build an override
  // that replaces no property, which set_route_action_override would then accept as a decision.
  if (entry.retry_policy == nullptr && entry.metadata_match_criteria == nullptr &&
      entry.shadow_policies.empty() && entry.hash_policy == nullptr &&
      entry.hedge_policy == nullptr && entry.rate_limit_policy == nullptr &&
      entry.cors_policy == nullptr) {
    return absl::InvalidArgumentError(
        "Route action override must replace at least one route action property");
  }
  return entry;
}

std::vector<Matchers::StringMatcherPtr> buildStringMatchers(
    const Protobuf::RepeatedPtrField<envoy::type::matcher::v3::StringMatcher>& matchers,
    Server::Configuration::ServerFactoryContext& context) {
  std::vector<Matchers::StringMatcherPtr> result;
  result.reserve(matchers.size());
  for (const auto& matcher : matchers) {
    result.push_back(std::make_unique<Matchers::StringMatcherImpl>(matcher, context));
  }
  return result;
}

std::optional<ShadowSettings> buildShadowSettings(const DynamicModuleRouteSpecifierProto& config) {
  if (!config.has_shadow_mode()) {
    return std::nullopt;
  }
  ShadowSettings settings;
  if (config.shadow_mode().compare_fields().empty()) {
    for (size_t field = 1; field < CompareFieldCount; field++) {
      if (field != envoy_dynamic_module_type_route_specifier_compare_field_DirectResponseBody &&
          field != envoy_dynamic_module_type_route_specifier_compare_field_RouteName) {
        settings.compare_mask |= 1ULL << field;
      }
    }
  } else {
    for (const int field : config.shadow_mode().compare_fields()) {
      settings.compare_mask |= 1ULL << field;
    }
    // The kind decides which of the other properties can be compared at all.
    settings.compare_mask |=
        1ULL << envoy_dynamic_module_type_route_specifier_compare_field_RouteKind;
  }
  settings.filter_names.assign(config.shadow_mode().filter_names().begin(),
                               config.shadow_mode().filter_names().end());
  return settings;
}

std::optional<RuntimeFraction>
buildRuntimeFraction(const DynamicModuleRouteSpecifierProto& config) {
  if (!config.has_runtime_fraction()) {
    return std::nullopt;
  }
  return RuntimeFraction{config.runtime_fraction().runtime_key(),
                         config.runtime_fraction().default_value()};
}

RouteKind routeKind(const Envoy::Router::RouteConstSharedPtr& route) {
  if (route == nullptr) {
    return envoy_dynamic_module_type_route_specifier_route_kind_None;
  }
  return route->routeEntry() != nullptr
             ? envoy_dynamic_module_type_route_specifier_route_kind_RouteEntry
             : envoy_dynamic_module_type_route_specifier_route_kind_DirectResponse;
}

// Layers the metadata a module recorded onto the metadata of the route. Returns nullptr when the
// module recorded none, so that both metadata accessors fall back to the route.
Envoy::Config::MetadataPackPtr<Envoy::Router::HttpRouteTypedMetadataFactory>
buildMetadataPack(const Envoy::Router::Route& route,
                  const envoy::config::core::v3::Metadata& overrides) {
  if (overrides.filter_metadata().empty() && overrides.typed_filter_metadata().empty()) {
    return nullptr;
  }
  envoy::config::core::v3::Metadata merged = route.metadata();
  // Merge per namespace so an entry replaces only its own key while the other keys of the route
  // stay in effect. A top-level merge would replace the whole namespace instead.
  for (const auto& [name, fields] : overrides.filter_metadata()) {
    (*merged.mutable_filter_metadata())[name].MergeFrom(fields);
  }
  for (const auto& [name, typed] : overrides.typed_filter_metadata()) {
    (*merged.mutable_typed_filter_metadata())[name] = typed;
  }
  // Building the pack runs the registered typed metadata factories, which throw on input they
  // reject. The caller turns that into a decision failure.
  return std::make_unique<Envoy::Router::RouteMetadataPack>(merged);
}

void applyHeaderMutations(Http::HeaderMap& headers,
                          const std::vector<HeaderMutation>& headers_to_add,
                          const std::vector<Http::LowerCaseString>& headers_to_remove) {
  for (const auto& mutation : headers_to_add) {
    switch (mutation.action) {
    case envoy_dynamic_module_type_route_specifier_header_append_action_AppendIfExistsOrAdd:
      headers.appendCopy(mutation.key, mutation.value);
      break;
    case envoy_dynamic_module_type_route_specifier_header_append_action_AddIfAbsent:
      if (headers.get(mutation.key).empty()) {
        headers.setCopy(mutation.key, mutation.value);
      }
      break;
    case envoy_dynamic_module_type_route_specifier_header_append_action_OverwriteIfExistsOrAdd:
      headers.setCopy(mutation.key, mutation.value);
      break;
    case envoy_dynamic_module_type_route_specifier_header_append_action_OverwriteIfExists:
      if (!headers.get(mutation.key).empty()) {
        headers.setCopy(mutation.key, mutation.value);
      }
      break;
    }
  }
  for (const auto& key : headers_to_remove) {
    headers.remove(key);
  }
}

void appendHeaderTransforms(Http::HeaderTransforms& transforms,
                            const std::vector<HeaderMutation>& headers_to_add,
                            const std::vector<Http::LowerCaseString>& headers_to_remove) {
  for (const auto& mutation : headers_to_add) {
    switch (mutation.action) {
    case envoy_dynamic_module_type_route_specifier_header_append_action_AppendIfExistsOrAdd:
      transforms.headers_to_append_or_add.emplace_back(mutation.key, mutation.value);
      break;
    case envoy_dynamic_module_type_route_specifier_header_append_action_AddIfAbsent:
      transforms.headers_to_add_if_absent.emplace_back(mutation.key, mutation.value);
      break;
    // HeaderTransforms cannot express OverwriteIfExists, so it is reported as an overwrite, the
    // same way HeaderParser reports it.
    case envoy_dynamic_module_type_route_specifier_header_append_action_OverwriteIfExistsOrAdd:
    case envoy_dynamic_module_type_route_specifier_header_append_action_OverwriteIfExists:
      transforms.headers_to_overwrite_or_add.emplace_back(mutation.key, mutation.value);
      break;
    }
  }
  for (const auto& key : headers_to_remove) {
    transforms.headers_to_remove.push_back(key);
  }
}

} // namespace

bool RouteOverrides::hasRouteEntryOverrides() const {
  return !cluster_name.empty() || timeout.has_value() || idle_timeout.has_value() ||
         max_stream_duration.has_value() || request_body_buffer_limit.has_value() ||
         priority.has_value() || cluster_not_found_response_code.has_value() ||
         route_action_override != nullptr || path.has_value() || host.has_value() ||
         !request_headers_to_add.empty() || !request_headers_to_remove.empty() ||
         !response_headers_to_add.empty() || !response_headers_to_remove.empty();
}

bool RouteOverrides::hasRouteOverrides() const {
  return !route_metadata.filter_metadata().empty() ||
         !route_metadata.typed_filter_metadata().empty() || !filter_disabled.empty();
}

DynamicModuleRouteSpecifierConfig::DynamicModuleRouteSpecifierConfig(
    const DynamicModuleRouteSpecifierProto& proto_config, absl::string_view specifier_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Envoy::Router::RouteSpecifierFactoryContext& context, absl::string_view metrics_namespace)
    : metrics_scope_(context.serverFactoryContext().serverScope().createScope(
          absl::StrCat(metrics_namespace, "."))),
      metrics_(*metrics_scope_), dynamic_module_(std::move(dynamic_module)),
      specifier_name_(proto_config.specifier_name()), specifier_config_(specifier_config),
      allowed_cluster_names_(buildStringMatchers(proto_config.allowed_cluster_names(),
                                                 context.serverFactoryContext())),
      allowed_filter_names_(
          buildStringMatchers(proto_config.allowed_filter_names(), context.serverFactoryContext())),
      allowed_metadata_namespaces_(buildStringMatchers(proto_config.allowed_metadata_namespaces(),
                                                       context.serverFactoryContext())),
      shadow_(buildShadowSettings(proto_config)),
      runtime_fraction_(buildRuntimeFraction(proto_config)),
      fail_closed_(proto_config.failure_policy() ==
                   envoy::extensions::router::route_specifiers::dynamic_modules::v3::NO_ROUTE),
      cluster_manager_(context.serverFactoryContext().clusterManager()),
      runtime_(context.serverFactoryContext().runtime()),
      time_source_(context.serverFactoryContext().timeSource()),
      stats_scope_(context.serverFactoryContext().serverScope().createScope(
          absl::StrCat(metrics_namespace, ".route_specifier.", proto_config.stat_prefix(), "."))),
      stats_{ALL_DYNAMIC_MODULE_ROUTE_SPECIFIER_STATS(POOL_COUNTER(*stats_scope_),
                                                      POOL_HISTOGRAM(*stats_scope_))},
      mismatch_counters_(CompareFieldCount, nullptr) {
  if (!shadow_.has_value()) {
    return;
  }
  for (size_t field = 1; field < CompareFieldCount; field++) {
    if ((shadow_->compare_mask & (1ULL << field)) != 0) {
      mismatch_counters_[field] = &Stats::Utility::counterFromElements(
          *stats_scope_,
          {Stats::DynamicName(absl::StrCat("shadow_mismatch_", CompareFieldNames[field]))});
    }
  }
}

DynamicModuleRouteSpecifierConfig::~DynamicModuleRouteSpecifierConfig() {
  if (in_module_config_ != nullptr) {
    // The destroy hook is resolved before the in-module configuration is created.
    ASSERT(on_config_destroy_ != nullptr);
    on_config_destroy_(in_module_config_);
  }
}

const DynamicModuleRouteSpecifierConfig::Template*
DynamicModuleRouteSpecifierConfig::routeTemplate(absl::string_view id) const {
  const auto it = templates_.find(id);
  return it != templates_.end() ? &it->second : nullptr;
}

const RouteActionOverride*
DynamicModuleRouteSpecifierConfig::routeActionOverride(absl::string_view name) const {
  const auto it = route_action_overrides_.find(name);
  return it != route_action_overrides_.end() ? &it->second : nullptr;
}

bool DynamicModuleRouteSpecifierConfig::registerRouteTemplate(absl::string_view id,
                                                              absl::string_view serialized_route) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (config_new_route_builder_ == nullptr || id.empty() || templates_.contains(id)) {
    return false;
  }
  envoy::config::route::v3::Route route;
  if (!route.ParseFromArray(serialized_route.data(), static_cast<int>(serialized_route.size()))) {
    return false;
  }
  absl::StatusOr<Envoy::Router::MatchableRouteConstSharedPtr> built =
      config_new_route_builder_->build(route, config_new_validate_clusters_);
  if (!built.ok()) {
    ENVOY_LOG_MISC(debug, "dynamic module route specifier could not register template '{}': {}", id,
                   built.status().message());
    return false;
  }
  const RouteKind kind = route.has_redirect() || route.has_direct_response()
                             ? envoy_dynamic_module_type_route_specifier_route_kind_DirectResponse
                             : envoy_dynamic_module_type_route_specifier_route_kind_RouteEntry;
  templates_.emplace(std::string(id), Template{std::string(id), std::move(built.value()), kind});
  template_ids_.emplace_back(id);
  return true;
}

namespace {

// An empty allowlist accepts every name.
bool allowed(const std::vector<Matchers::StringMatcherPtr>& matchers, absl::string_view name) {
  if (matchers.empty()) {
    return true;
  }
  return std::any_of(matchers.begin(), matchers.end(),
                     [name](const auto& matcher) { return matcher->match(name); });
}

} // namespace

bool DynamicModuleRouteSpecifierConfig::clusterNameAllowed(absl::string_view name) const {
  return allowed(allowed_cluster_names_, name);
}

bool DynamicModuleRouteSpecifierConfig::metadataNamespaceAllowed(absl::string_view name) const {
  return allowed(allowed_metadata_namespaces_, name);
}

bool DynamicModuleRouteSpecifierConfig::filterNameAllowed(absl::string_view name) const {
  return allowed(allowed_filter_names_, name);
}

Stats::Counter* DynamicModuleRouteSpecifierConfig::mismatchCounter(uint32_t compare_field) const {
  return compare_field < mismatch_counters_.size() ? mismatch_counters_[compare_field] : nullptr;
}

absl::StatusOr<DynamicModuleRouteSpecifierConfigSharedPtr>
newDynamicModuleRouteSpecifierConfig(const DynamicModuleRouteSpecifierProto& proto_config,
                                     Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                     Envoy::Router::RouteSpecifierFactoryContext& context) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  if (!proto_config.has_shadow_mode() && proto_config.failure_policy() ==
                                             envoy::extensions::router::route_specifiers::
                                                 dynamic_modules::v3::FAILURE_POLICY_UNSPECIFIED) {
    return absl::InvalidArgumentError(
        "failure_policy must be set unless the route specifier runs in shadow mode");
  }
  if (!proto_config.route_templates().empty() && !context.routeBuilder().has_value()) {
    return absl::InvalidArgumentError(
        "route templates require a route specifier configured on a virtual host or a route");
  }

  auto on_config_new = dynamic_module->getFunctionPointer<OnRouteSpecifierConfigNewType>(
      "envoy_dynamic_module_on_route_specifier_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnRouteSpecifierConfigDestroyType>(
      "envoy_dynamic_module_on_route_specifier_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_route = dynamic_module->getFunctionPointer<OnRouteSpecifierOnRouteType>(
      "envoy_dynamic_module_on_route_specifier_on_route");
  RETURN_IF_NOT_OK_REF(on_route.status());

  // Use knownAnyToBytes() to properly handle StringValue/BytesValue/Struct types.
  std::string specifier_config;
  if (proto_config.has_specifier_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.specifier_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    specifier_config = std::move(config_or_error.value());
  }

  const std::string metrics_namespace =
      proto_config.dynamic_module_config().metrics_namespace().empty()
          ? std::string(DefaultMetricsNamespace)
          : proto_config.dynamic_module_config().metrics_namespace();
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
    context.serverFactoryContext().api().customStatNamespaces().registerStatNamespace(
        metrics_namespace);
  }

  auto config = std::make_shared<DynamicModuleRouteSpecifierConfig>(
      proto_config, specifier_config, std::move(dynamic_module), context, metrics_namespace);
  config->on_config_destroy_ = on_config_destroy.value();
  config->on_route_ = on_route.value();
  // The shadow result hook is optional, so an unresolved symbol only means the module does not
  // report the comparison itself.
  auto on_shadow_result =
      config->dynamic_module_->getFunctionPointer<OnRouteSpecifierShadowResultType>(
          "envoy_dynamic_module_on_route_specifier_shadow_result");
  if (on_shadow_result.ok()) {
    config->on_shadow_result_ = on_shadow_result.value();
  }

  const bool validate_clusters =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, validate_clusters, false);
  auto route_builder = context.routeBuilder();
  for (const auto& route_template : proto_config.route_templates()) {
    const std::string& id = route_template.template_id();
    if (config->templates_.contains(id)) {
      return absl::InvalidArgumentError(fmt::format("duplicate route template id '{}'", id));
    }
    absl::StatusOr<Envoy::Router::MatchableRouteConstSharedPtr> built =
        route_builder->build(route_template.route(), validate_clusters);
    if (!built.ok()) {
      return absl::InvalidArgumentError(
          fmt::format("route template '{}': {}", id, built.status().message()));
    }
    const RouteKind kind =
        route_template.route().has_redirect() || route_template.route().has_direct_response()
            ? envoy_dynamic_module_type_route_specifier_route_kind_DirectResponse
            : envoy_dynamic_module_type_route_specifier_route_kind_RouteEntry;
    config->templates_.emplace(
        id, DynamicModuleRouteSpecifierConfig::Template{id, std::move(built.value()), kind});
    config->template_ids_.push_back(id);
  }

  config->route_action_overrides_.reserve(proto_config.route_action_overrides().size());
  for (const auto& [name, proto_override] : proto_config.route_action_overrides()) {
    auto entry_or_error = buildRouteActionOverride(proto_override, context.serverFactoryContext());
    if (!entry_or_error.ok()) {
      return absl::InvalidArgumentError(
          fmt::format("route action override '{}': {}", name, entry_or_error.status().message()));
    }
    if (validate_clusters) {
      for (const auto& shadow_policy : entry_or_error.value().shadow_policies) {
        // A policy that names its cluster through a request header resolves it per request, so only
        // a statically named cluster can be checked here.
        if (!shadow_policy->cluster().empty() &&
            !context.serverFactoryContext().clusterManager().hasCluster(shadow_policy->cluster())) {
          return absl::InvalidArgumentError(
              fmt::format("route action override '{}': unknown shadow cluster '{}'", name,
                          shadow_policy->cluster()));
        }
      }
    }
    config->route_action_overrides_.emplace(name, std::move(entry_or_error.value()));
  }

  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = config->specifier_name_.data(),
                                                     .length = config->specifier_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buf = {.ptr = config->specifier_config_.data(),
                                                       .length = config->specifier_config_.size()};
  // Expose the route builder to the module so it can register templates while the configuration is
  // created. It borrows the init manager of the configuration, so it is cleared once the hook
  // returns.
  config->config_new_validate_clusters_ = validate_clusters;
  config->config_new_route_builder_ = route_builder.ptr();
  config->in_module_config_ =
      (*on_config_new.value())(static_cast<void*>(config.get()), name_buf, config_buf);
  config->config_new_route_builder_ = nullptr;
  config->stat_creation_frozen_.store(true, std::memory_order_release);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module route specifier config");
  }
  return config;
}

DynamicModuleRoute::DynamicModuleRoute(Envoy::Router::RouteConstSharedPtr route,
                                       DynamicModuleRouteSpecifierConfigSharedPtr config,
                                       RouteOverrides&& overrides)
    : Envoy::Router::DelegatingRoute(std::move(route)), config_(std::move(config)),
      overrides_(std::move(overrides)),
      metadata_pack_(buildMetadataPack(*base_route_, overrides_.route_metadata)) {}

const envoy::config::core::v3::Metadata& DynamicModuleRoute::metadata() const {
  return metadata_pack_ != nullptr ? metadata_pack_->proto_metadata_
                                   : Envoy::Router::DelegatingRoute::metadata();
}

const Envoy::Config::TypedMetadata& DynamicModuleRoute::typedMetadata() const {
  return metadata_pack_ != nullptr ? metadata_pack_->typed_metadata_
                                   : Envoy::Router::DelegatingRoute::typedMetadata();
}

std::optional<bool> DynamicModuleRoute::filterDisabled(absl::string_view name) const {
  const auto it = overrides_.filter_disabled.find(name);
  return it != overrides_.filter_disabled.end()
             ? std::optional<bool>(it->second)
             : Envoy::Router::DelegatingRoute::filterDisabled(name);
}

DynamicModuleRouteEntry::DynamicModuleRouteEntry(Envoy::Router::RouteConstSharedPtr route,
                                                 DynamicModuleRouteSpecifierConfigSharedPtr config,
                                                 RouteOverrides&& overrides)
    : DelegatingRouteEntry(std::move(route)), config_(std::move(config)),
      overrides_(std::move(overrides)),
      metadata_pack_(buildMetadataPack(*base_route_, overrides_.route_metadata)) {}

const envoy::config::core::v3::Metadata& DynamicModuleRouteEntry::metadata() const {
  return metadata_pack_ != nullptr ? metadata_pack_->proto_metadata_
                                   : DelegatingRouteEntry::metadata();
}

const Envoy::Config::TypedMetadata& DynamicModuleRouteEntry::typedMetadata() const {
  return metadata_pack_ != nullptr ? metadata_pack_->typed_metadata_
                                   : DelegatingRouteEntry::typedMetadata();
}

std::optional<bool> DynamicModuleRouteEntry::filterDisabled(absl::string_view name) const {
  const auto it = overrides_.filter_disabled.find(name);
  return it != overrides_.filter_disabled.end() ? std::optional<bool>(it->second)
                                                : DelegatingRouteEntry::filterDisabled(name);
}

const std::string& DynamicModuleRouteEntry::clusterName() const {
  return overrides_.cluster_name.empty() ? DelegatingRouteEntry::clusterName()
                                         : overrides_.cluster_name;
}

std::chrono::milliseconds DynamicModuleRouteEntry::timeout() const {
  return overrides_.timeout.value_or(DelegatingRouteEntry::timeout());
}

std::optional<std::chrono::milliseconds> DynamicModuleRouteEntry::idleTimeout() const {
  return overrides_.idle_timeout.has_value() ? overrides_.idle_timeout
                                             : DelegatingRouteEntry::idleTimeout();
}

std::optional<std::chrono::milliseconds> DynamicModuleRouteEntry::maxStreamDuration() const {
  return overrides_.max_stream_duration.has_value() ? overrides_.max_stream_duration
                                                    : DelegatingRouteEntry::maxStreamDuration();
}

bool DynamicModuleRouteEntry::usingNewTimeouts() const {
  return overrides_.max_stream_duration.has_value() || DelegatingRouteEntry::usingNewTimeouts();
}

uint64_t DynamicModuleRouteEntry::requestBodyBufferLimit() const {
  return overrides_.request_body_buffer_limit.value_or(
      DelegatingRouteEntry::requestBodyBufferLimit());
}

Upstream::ResourcePriority DynamicModuleRouteEntry::priority() const {
  return overrides_.priority.value_or(DelegatingRouteEntry::priority());
}

Http::Code DynamicModuleRouteEntry::clusterNotFoundResponseCode() const {
  return overrides_.cluster_not_found_response_code.value_or(
      DelegatingRouteEntry::clusterNotFoundResponseCode());
}

const Envoy::Router::RetryPolicyConstSharedPtr& DynamicModuleRouteEntry::retryPolicy() const {
  const RouteActionOverride* entry = overrides_.route_action_override;
  return entry != nullptr && entry->retry_policy != nullptr ? entry->retry_policy
                                                            : DelegatingRouteEntry::retryPolicy();
}

const Envoy::Router::MetadataMatchCriteria* DynamicModuleRouteEntry::metadataMatchCriteria() const {
  const RouteActionOverride* entry = overrides_.route_action_override;
  return entry != nullptr && entry->metadata_match_criteria != nullptr
             ? entry->metadata_match_criteria.get()
             : DelegatingRouteEntry::metadataMatchCriteria();
}

const std::vector<Envoy::Router::ShadowPolicyPtr>& DynamicModuleRouteEntry::shadowPolicies() const {
  const RouteActionOverride* entry = overrides_.route_action_override;
  return entry != nullptr && !entry->shadow_policies.empty()
             ? entry->shadow_policies
             : DelegatingRouteEntry::shadowPolicies();
}

const Http::HashPolicy* DynamicModuleRouteEntry::hashPolicy() const {
  const RouteActionOverride* entry = overrides_.route_action_override;
  return entry != nullptr && entry->hash_policy != nullptr ? entry->hash_policy.get()
                                                           : DelegatingRouteEntry::hashPolicy();
}

const Envoy::Router::HedgePolicy& DynamicModuleRouteEntry::hedgePolicy() const {
  const RouteActionOverride* entry = overrides_.route_action_override;
  return entry != nullptr && entry->hedge_policy != nullptr ? *entry->hedge_policy
                                                            : DelegatingRouteEntry::hedgePolicy();
}

const Envoy::Router::RateLimitPolicy& DynamicModuleRouteEntry::rateLimitPolicy() const {
  const RouteActionOverride* entry = overrides_.route_action_override;
  return entry != nullptr && entry->rate_limit_policy != nullptr
             ? *entry->rate_limit_policy
             : DelegatingRouteEntry::rateLimitPolicy();
}

const Envoy::Router::CorsPolicy* DynamicModuleRouteEntry::corsPolicy() const {
  const RouteActionOverride* entry = overrides_.route_action_override;
  return entry != nullptr && entry->cors_policy != nullptr ? entry->cors_policy.get()
                                                           : DelegatingRouteEntry::corsPolicy();
}

std::string DynamicModuleRouteEntry::currentUrlPathAfterRewrite(
    const Http::RequestHeaderMap& headers, const Formatter::Context& context,
    const StreamInfo::StreamInfo& stream_info) const {
  return overrides_.path.value_or(
      DelegatingRouteEntry::currentUrlPathAfterRewrite(headers, context, stream_info));
}

void DynamicModuleRouteEntry::finalizeRequestHeaders(Http::RequestHeaderMap& headers,
                                                     const Formatter::Context& context,
                                                     const StreamInfo::StreamInfo& stream_info,
                                                     bool insert_envoy_original_path) const {
  DelegatingRouteEntry::finalizeRequestHeaders(headers, context, stream_info,
                                               insert_envoy_original_path);
  applyHeaderMutations(headers, overrides_.request_headers_to_add,
                       overrides_.request_headers_to_remove);
  if (overrides_.host.has_value()) {
    Http::Utility::updateAuthority(headers, *overrides_.host, appendXfh(),
                                   insert_envoy_original_path);
  }
  if (overrides_.path.has_value()) {
    // The route records the original path when it rewrites the path itself, so only record it here
    // when it did not.
    if (insert_envoy_original_path && headers.EnvoyOriginalPath() == nullptr) {
      headers.setEnvoyOriginalPath(headers.getPathValue());
    }
    headers.setPath(*overrides_.path);
  }
}

Http::HeaderTransforms
DynamicModuleRouteEntry::requestHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                 bool do_formatting) const {
  Http::HeaderTransforms transforms =
      DelegatingRouteEntry::requestHeaderTransforms(stream_info, do_formatting);
  appendHeaderTransforms(transforms, overrides_.request_headers_to_add,
                         overrides_.request_headers_to_remove);
  return transforms;
}

void DynamicModuleRouteEntry::finalizeResponseHeaders(
    Http::ResponseHeaderMap& headers, const Formatter::Context& context,
    const StreamInfo::StreamInfo& stream_info) const {
  DelegatingRouteEntry::finalizeResponseHeaders(headers, context, stream_info);
  applyHeaderMutations(headers, overrides_.response_headers_to_add,
                       overrides_.response_headers_to_remove);
}

Http::HeaderTransforms
DynamicModuleRouteEntry::responseHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                  bool do_formatting) const {
  Http::HeaderTransforms transforms =
      DelegatingRouteEntry::responseHeaderTransforms(stream_info, do_formatting);
  appendHeaderTransforms(transforms, overrides_.response_headers_to_add,
                         overrides_.response_headers_to_remove);
  return transforms;
}

Envoy::Router::OnRouteResult DynamicModuleRouteSpecifier::onRoute(
    Envoy::Router::RouteConstSharedPtr route, const Http::RequestHeaderMap& headers,
    const StreamInfo::StreamInfo& stream_info, uint64_t random) const {
  const MonotonicTime start = config_->timeSource().monotonicTime();
  const auto record_duration = [&](Stats::Histogram& histogram) {
    histogram.recordValue(std::chrono::duration_cast<std::chrono::microseconds>(
                              config_->timeSource().monotonicTime() - start)
                              .count());
  };

  const auto& runtime_fraction = config_->runtimeFraction();
  if (runtime_fraction.has_value() &&
      !config_->runtime().snapshot().featureEnabled(runtime_fraction->key,
                                                    runtime_fraction->default_value, random)) {
    config_->stats().runtime_skipped_.inc();
    record_duration(config_->stats().specifier_duration_);
    return {std::move(route)};
  }

  RouteSpecifierContext context{*config_, route, headers, stream_info, random};
  // The module can return a decision this build does not know, for example from a newer ABI, so it
  // is read as its underlying integer. Loading an out of range enum value is undefined behavior.
  const uint32_t decision = static_cast<uint32_t>(
      config_->on_route_(config_->in_module_config_, static_cast<void*>(&context)));
  // Reading the clock is the most expensive thing this method does that is not the module itself,
  // so the start of the specifier doubles as the start of the module. Only the runtime fraction
  // check and the context construction sit between the two, which is why on_route_duration is
  // measured from there rather than read again.
  const MonotonicTime module_end = config_->timeSource().monotonicTime();
  config_->stats().on_route_duration_.recordValue(
      std::chrono::duration_cast<std::chrono::microseconds>(module_end - start).count());

  const bool shadow = config_->shadow().has_value();
  // Resolving the decision moves the recorded overrides onto the route it produces, so the filter
  // names the module recorded are taken while they are still there.
  std::vector<std::string> module_filter_names;
  if (shadow) {
    module_filter_names.reserve(context.overrides.filter_disabled.size());
    for (const auto& [name, disabled] : context.overrides.filter_disabled) {
      module_filter_names.push_back(name);
    }
  }

  Decision result = resolve(context, decision);

  if (shadow) {
    const bool pass_through =
        decision == envoy_dynamic_module_type_route_specifier_decision_PassThrough;
    uint64_t mismatches = 0;
    if (result.failure != envoy_dynamic_module_type_route_specifier_failure_None) {
      config_->stats().shadow_failure_.inc();
    } else if (pass_through) {
      config_->stats().shadow_pass_through_.inc();
    } else {
      mismatches = compare(result.route, context, module_filter_names);
      if (mismatches == 0) {
        config_->stats().shadow_match_.inc();
      } else {
        config_->stats().shadow_mismatch_.inc();
      }
    }
    if (config_->on_shadow_result_ != nullptr) {
      context.setters_enabled = false;
      // A module built against a newer ABI could have returned a decision this build does not
      // know, which resolve() already reported as a module error, so it is reported as one here
      // too rather than handed back as an unknown value.
      config_->on_shadow_result_(
          config_->in_module_config_, static_cast<void*>(&context),
          result.failure == envoy_dynamic_module_type_route_specifier_failure_ModuleError
              ? envoy_dynamic_module_type_route_specifier_decision_Error
              : static_cast<ModuleDecision>(decision),
          result.failure, mismatches);
    }
    record_duration(config_->stats().specifier_duration_);
    return {std::move(route)};
  }

  record_duration(config_->stats().specifier_duration_);
  return {std::move(result.route), result.status};
}

DynamicModuleRouteSpecifier::Decision
DynamicModuleRouteSpecifier::resolve(RouteSpecifierContext& context, uint32_t decision) const {
  using Status = Envoy::Router::OnRouteResultStatus;
  const auto status = [&context](Status by_decision) {
    switch (context.chain_status) {
    case envoy_dynamic_module_type_route_specifier_chain_status_Continue:
      return Status::Continue;
    case envoy_dynamic_module_type_route_specifier_chain_status_StopIteration:
      return Status::StopIteration;
    case envoy_dynamic_module_type_route_specifier_chain_status_Default:
      break;
    }
    return by_decision;
  };
  const auto fail = [this, &context](Failure failure) {
    switch (failure) {
    case envoy_dynamic_module_type_route_specifier_failure_ModuleError:
      config_->stats().failure_module_error_.inc();
      break;
    case envoy_dynamic_module_type_route_specifier_failure_TemplateNotSelected:
      config_->stats().failure_template_not_selected_.inc();
      break;
    case envoy_dynamic_module_type_route_specifier_failure_TemplateMatchFailed:
      config_->stats().failure_template_match_failed_.inc();
      break;
    case envoy_dynamic_module_type_route_specifier_failure_OverrideWithoutRoute:
      config_->stats().failure_override_without_route_.inc();
      break;
    case envoy_dynamic_module_type_route_specifier_failure_OverrideOnNonRouteEntry:
      config_->stats().failure_override_on_non_route_entry_.inc();
      break;
    case envoy_dynamic_module_type_route_specifier_failure_RouteMetadata:
      config_->stats().failure_route_metadata_.inc();
      break;
    case envoy_dynamic_module_type_route_specifier_failure_None:
      IS_ENVOY_BUG("route specifier failure without a reason");
      break;
    }
    ENVOY_LOG(debug, "dynamic module route specifier could not honor the decision, reason {}",
              static_cast<int>(failure));
    if (config_->failClosed()) {
      return Decision{nullptr, Status::StopIteration, failure};
    }
    return Decision{context.input_route, Status::Continue, failure};
  };

  switch (decision) {
  case envoy_dynamic_module_type_route_specifier_decision_PassThrough:
    config_->stats().decision_pass_through_.inc();
    return {context.input_route, status(Status::Continue)};
  case envoy_dynamic_module_type_route_specifier_decision_NoRoute:
    config_->stats().decision_no_route_.inc();
    return {nullptr, status(Status::StopIteration)};
  case envoy_dynamic_module_type_route_specifier_decision_Error:
    config_->stats().decision_error_.inc();
    return fail(envoy_dynamic_module_type_route_specifier_failure_ModuleError);
  case envoy_dynamic_module_type_route_specifier_decision_Override: {
    config_->stats().decision_override_.inc();
    if (context.input_route == nullptr) {
      return fail(envoy_dynamic_module_type_route_specifier_failure_OverrideWithoutRoute);
    }
    Decision wrapped = wrap(context.input_route, context, status(Status::Continue));
    return wrapped.failure == envoy_dynamic_module_type_route_specifier_failure_None
               ? wrapped
               : fail(wrapped.failure);
  }
  case envoy_dynamic_module_type_route_specifier_decision_SelectTemplate: {
    config_->stats().decision_select_template_.inc();
    if (context.selected_template == nullptr) {
      return fail(envoy_dynamic_module_type_route_specifier_failure_TemplateNotSelected);
    }
    // set_template evaluated the template against the request, so reuse the result rather than
    // matching a second time. A null result means the match did not hold for the request.
    if (context.selected_route == nullptr) {
      return fail(envoy_dynamic_module_type_route_specifier_failure_TemplateMatchFailed);
    }
    Decision wrapped = wrap(context.selected_route, context, status(Status::StopIteration));
    return wrapped.failure == envoy_dynamic_module_type_route_specifier_failure_None
               ? wrapped
               : fail(wrapped.failure);
  }
  }
  // A module built against a newer ABI could return a decision this build does not know.
  config_->stats().decision_error_.inc();
  return fail(envoy_dynamic_module_type_route_specifier_failure_ModuleError);
}

DynamicModuleRouteSpecifier::Decision
DynamicModuleRouteSpecifier::wrap(Envoy::Router::RouteConstSharedPtr route,
                                  RouteSpecifierContext& context,
                                  Envoy::Router::OnRouteResultStatus status) const {
  const bool route_entry_overrides = context.overrides.hasRouteEntryOverrides();
  if (!route_entry_overrides && !context.overrides.hasRouteOverrides()) {
    return {std::move(route), status};
  }
  if (route_entry_overrides && route->routeEntry() == nullptr) {
    return {nullptr, status,
            envoy_dynamic_module_type_route_specifier_failure_OverrideOnNonRouteEntry};
  }
  // Building the metadata pack runs the registered typed metadata factories, which throw on input
  // they reject, so a module cannot reach the worker with metadata Envoy cannot parse.
  Decision decision{nullptr, status};
  TRY_NEEDS_AUDIT {
    if (route_entry_overrides) {
      decision.route = std::make_shared<DynamicModuleRouteEntry>(std::move(route), config_,
                                                                 std::move(context.overrides));
    } else {
      decision.route = std::make_shared<DynamicModuleRoute>(std::move(route), config_,
                                                            std::move(context.overrides));
    }
  }
  END_TRY
  CATCH(const EnvoyException& e, {
    ENVOY_LOG_EVERY_POW_2(warn, "dynamic module route metadata was rejected: {}", e.what());
    decision.failure = envoy_dynamic_module_type_route_specifier_failure_RouteMetadata;
  });
  return decision;
}

namespace {

using HeaderPairs = std::vector<std::pair<std::string, std::string>>;

// The headers of a finalized request, split into the parts that are compared separately.
struct FinalizedRequest {
  std::string path;
  std::string original_path;
  std::string authority;
  std::string original_host;
  std::string forwarded_host;
  HeaderPairs other_headers;
};

FinalizedRequest finalizeRequest(const Envoy::Router::Route& route,
                                 const Http::RequestHeaderMap& headers,
                                 const StreamInfo::StreamInfo& stream_info) {
  auto copy = Http::createHeaderMap<Http::RequestHeaderMapImpl>(headers);
  Formatter::Context context(copy.get());
  route.routeEntry()->finalizeRequestHeaders(*copy, context, stream_info, true);

  FinalizedRequest result;
  result.path = copy->getPathValue();
  result.original_path = copy->getEnvoyOriginalPathValue();
  result.authority = copy->getHostValue();
  result.original_host = copy->getEnvoyOriginalHostValue();
  result.forwarded_host = copy->getForwardedHostValue();
  copy->iterate([&result](const Http::HeaderEntry& entry) {
    const absl::string_view key = entry.key().getStringView();
    if (key != Http::Headers::get().Path.get() && key != Http::Headers::get().Host.get() &&
        key != Http::Headers::get().EnvoyOriginalPath.get() &&
        key != Http::Headers::get().EnvoyOriginalHost.get() &&
        key != Http::Headers::get().ForwardedHost.get()) {
      result.other_headers.emplace_back(key, entry.value().getStringView());
    }
    return Http::HeaderMap::Iterate::Continue;
  });
  std::sort(result.other_headers.begin(), result.other_headers.end());
  return result;
}

HeaderPairs finalizeResponse(const Envoy::Router::Route& route,
                             const Http::RequestHeaderMap& headers,
                             const StreamInfo::StreamInfo& stream_info) {
  auto response = Http::ResponseHeaderMapImpl::create();
  Formatter::Context context(&headers, response.get());
  route.routeEntry()->finalizeResponseHeaders(*response, context, stream_info);

  HeaderPairs result;
  response->iterate([&result](const Http::HeaderEntry& entry) {
    result.emplace_back(entry.key().getStringView(), entry.value().getStringView());
    return Http::HeaderMap::Iterate::Continue;
  });
  std::sort(result.begin(), result.end());
  return result;
}

// The cookies a hash policy would write, recorded instead of written so that comparing the policies
// of two routes has no effect on the request.
struct HashResult {
  std::optional<uint64_t> hash;
  HeaderPairs cookies;
};

HashResult generateHash(const Http::HashPolicy& policy, const Http::RequestHeaderMap& headers,
                        const StreamInfo::StreamInfo& stream_info) {
  HashResult result;
  result.hash = policy.generateHash(
      headers, stream_info,
      [&result](absl::string_view name, absl::string_view path, std::chrono::seconds ttl,
                absl::Span<const Http::CookieAttribute> attributes) {
        std::string value = absl::StrCat(path, ";", ttl.count());
        for (const auto& attribute : attributes) {
          absl::StrAppend(&value, ";", attribute.name_, "=", attribute.value_);
        }
        result.cookies.emplace_back(name, std::move(value));
        return "shadow";
      });
  std::sort(result.cookies.begin(), result.cookies.end());
  return result;
}

bool retryPoliciesEqual(const Envoy::Router::RetryPolicy& lhs,
                        const Envoy::Router::RetryPolicy& rhs) {
  return lhs.retryOn() == rhs.retryOn() && lhs.numRetries() == rhs.numRetries() &&
         lhs.perTryTimeout() == rhs.perTryTimeout() &&
         lhs.perTryIdleTimeout() == rhs.perTryIdleTimeout() &&
         lhs.retriableStatusCodes() == rhs.retriableStatusCodes() &&
         lhs.hostSelectionMaxAttempts() == rhs.hostSelectionMaxAttempts() &&
         lhs.baseInterval() == rhs.baseInterval() && lhs.maxInterval() == rhs.maxInterval() &&
         lhs.resetMaxInterval() == rhs.resetMaxInterval() &&
         lhs.refreshClusterOnRetry() == rhs.refreshClusterOnRetry() &&
         // The extension backed parts expose no state to compare, so only their shape is.
         lhs.retriableHeaders().size() == rhs.retriableHeaders().size() &&
         lhs.retriableRequestHeaders().size() == rhs.retriableRequestHeaders().size() &&
         lhs.resetHeaders().size() == rhs.resetHeaders().size() &&
         lhs.retryHostPredicates().size() == rhs.retryHostPredicates().size() &&
         lhs.retryOptionsPredicates().size() == rhs.retryOptionsPredicates().size() &&
         (lhs.retryPriority() == nullptr) == (rhs.retryPriority() == nullptr);
}

bool metadataMatchCriteriaEqual(const Envoy::Router::MetadataMatchCriteria* lhs,
                                const Envoy::Router::MetadataMatchCriteria* rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return lhs == rhs;
  }
  const auto& lhs_criteria = lhs->metadataMatchCriteria();
  const auto& rhs_criteria = rhs->metadataMatchCriteria();
  if (lhs_criteria.size() != rhs_criteria.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs_criteria.size(); i++) {
    if (lhs_criteria[i]->name() != rhs_criteria[i]->name() ||
        !(lhs_criteria[i]->value() == rhs_criteria[i]->value())) {
      return false;
    }
  }
  return true;
}

bool shadowPoliciesEqual(const std::vector<Envoy::Router::ShadowPolicyPtr>& lhs,
                         const std::vector<Envoy::Router::ShadowPolicyPtr>& rhs,
                         const Http::RequestHeaderMap& headers,
                         const StreamInfo::StreamInfo& stream_info) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); i++) {
    if (lhs[i]->cluster() != rhs[i]->cluster() ||
        lhs[i]->clusterHeader() != rhs[i]->clusterHeader() ||
        lhs[i]->runtimeKey() != rhs[i]->runtimeKey() ||
        !Protobuf::util::MessageDifferencer::Equals(lhs[i]->defaultValue(),
                                                    rhs[i]->defaultValue()) ||
        lhs[i]->traceSampled() != rhs[i]->traceSampled() ||
        lhs[i]->disableShadowHostSuffixAppend() != rhs[i]->disableShadowHostSuffixAppend() ||
        lhs[i]->hostRewriteLiteral() != rhs[i]->hostRewriteLiteral()) {
      return false;
    }
    // The header evaluator is an extension, so compare the headers it would add to a mirror.
    const auto evaluate = [&](const Envoy::Router::ShadowPolicy& policy) {
      auto copy = Http::createHeaderMap<Http::RequestHeaderMapImpl>(headers);
      Formatter::Context context(copy.get());
      policy.headerEvaluator().evaluateHeaders(*copy, context, stream_info);
      HeaderPairs result;
      copy->iterate([&result](const Http::HeaderEntry& entry) {
        result.emplace_back(entry.key().getStringView(), entry.value().getStringView());
        return Http::HeaderMap::Iterate::Continue;
      });
      std::sort(result.begin(), result.end());
      return result;
    };
    if (evaluate(*lhs[i]) != evaluate(*rhs[i])) {
      return false;
    }
  }
  return true;
}

// A rate limit policy reduced to a sorted signature per applicable entry, so the two policies
// compare regardless of entry order. Each entry contributes its stage, disable key, stream done
// flag and the descriptors it generates for the request. toString covers the descriptor entries, so
// the limit override and the option are added on top of it. Rate limit stages are validated to
// [0, 10]. Route backed descriptors read the resolved route, which is the same for both policies.
std::vector<std::string> rateLimitSignatures(const Envoy::Router::RateLimitPolicy& policy,
                                             const Http::RequestHeaderMap& headers,
                                             const StreamInfo::StreamInfo& stream_info) {
  std::vector<std::string> result;
  for (uint64_t stage = 0; stage <= 10; stage++) {
    for (const auto& entry : policy.getApplicableRateLimit(stage)) {
      const Envoy::Router::RateLimitPolicyEntry& rate_limit = entry.get();
      std::vector<RateLimit::Descriptor> descriptors;
      std::vector<RateLimit::LocalDescriptor> local_descriptors;
      rate_limit.populateDescriptors(descriptors, "", headers, stream_info);
      rate_limit.populateLocalDescriptors(local_descriptors, "", headers, stream_info);
      std::string signature = absl::StrCat(rate_limit.stage(), "|", rate_limit.disableKey(), "|",
                                           rate_limit.applyOnStreamDone() ? 1 : 0);
      for (const auto& descriptor : descriptors) {
        absl::StrAppend(&signature, "|", descriptor.toString(),
                        "|opt=", static_cast<int>(descriptor.x_ratelimit_option_));
        if (descriptor.limit_.has_value()) {
          absl::StrAppend(&signature, "|limit=", descriptor.limit_->requests_per_unit_, "/",
                          static_cast<int>(descriptor.limit_->unit_));
        }
      }
      for (const auto& descriptor : local_descriptors) {
        absl::StrAppend(&signature, "|local:", descriptor.toString());
      }
      result.push_back(signature);
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

// A CORS policy reduced to its comparable fields. Origin matchers are extension backed, so only
// their count is compared. The enabled and shadow enabled states are left out because they resolve
// a runtime fraction that draws a fresh value on every read. Null reduces to the empty string,
// which differs from a present policy that sets nothing.
std::string corsSignature(const Envoy::Router::CorsPolicy* policy) {
  if (policy == nullptr) {
    return "";
  }
  const auto optional_bool = [](const std::optional<bool>& value) -> absl::string_view {
    return value.has_value() ? (*value ? "1" : "0") : "";
  };
  return absl::StrCat(policy->allowOrigins().size(), "|", policy->allowMethods(), "|",
                      policy->allowHeaders(), "|", policy->exposeHeaders(), "|", policy->maxAge(),
                      "|", optional_bool(policy->allowCredentials()), "|",
                      optional_bool(policy->allowPrivateNetworkAccess()), "|",
                      optional_bool(policy->forwardNotMatchingPreflights()));
}

// A route tracing configuration reduced to its comparable fields. Custom tags are extension backed,
// so only their count is compared. Null reduces to the empty fields.
std::string tracingSignature(const Envoy::Router::RouteTracing* tracing) {
  if (tracing == nullptr) {
    return "";
  }
  const auto fraction = [](const envoy::type::v3::FractionalPercent& value) {
    return absl::StrCat(value.numerator(), "/", value.denominator());
  };
  return absl::StrCat(
      fraction(tracing->getClientSampling()), "|", fraction(tracing->getRandomSampling()), "|",
      fraction(tracing->getOverallSampling()), "|", tracing->getCustomTags().size());
}

} // namespace

uint64_t
DynamicModuleRouteSpecifier::compare(const Envoy::Router::RouteConstSharedPtr& shadow,
                                     const RouteSpecifierContext& context,
                                     const std::vector<std::string>& module_filter_names) const {
  const Envoy::Router::RouteConstSharedPtr& input = context.input_route;
  const uint64_t mask = config_->shadow()->compare_mask;
  uint64_t mismatches = 0;
  const auto compares = [mask](uint32_t field) { return (mask & (1ULL << field)) != 0; };
  const auto mark = [this, &mismatches](uint32_t field) {
    mismatches |= 1ULL << field;
    if (Stats::Counter* counter = config_->mismatchCounter(field); counter != nullptr) {
      counter->inc();
    }
  };

  if (routeKind(shadow) != routeKind(input)) {
    mark(envoy_dynamic_module_type_route_specifier_compare_field_RouteKind);
    return mismatches;
  }
  if (shadow == nullptr) {
    return mismatches;
  }

  if (compares(envoy_dynamic_module_type_route_specifier_compare_field_RouteName) &&
      shadow->routeName() != input->routeName()) {
    mark(envoy_dynamic_module_type_route_specifier_compare_field_RouteName);
  }
  if (compares(envoy_dynamic_module_type_route_specifier_compare_field_VirtualHostName) &&
      shadow->virtualHost().name() != input->virtualHost().name()) {
    mark(envoy_dynamic_module_type_route_specifier_compare_field_VirtualHostName);
  }
  if (compares(envoy_dynamic_module_type_route_specifier_compare_field_RouteMetadata) &&
      !Protobuf::util::MessageDifferencer::Equals(shadow->metadata(), input->metadata())) {
    mark(envoy_dynamic_module_type_route_specifier_compare_field_RouteMetadata);
  }
  if (compares(envoy_dynamic_module_type_route_specifier_compare_field_Tracing) &&
      tracingSignature(shadow->tracingConfig()) != tracingSignature(input->tracingConfig())) {
    mark(envoy_dynamic_module_type_route_specifier_compare_field_Tracing);
  }
  if (compares(envoy_dynamic_module_type_route_specifier_compare_field_FilterDisabled)) {
    std::vector<absl::string_view> names;
    names.reserve(config_->shadow()->filter_names.size() + module_filter_names.size());
    for (const auto& name : config_->shadow()->filter_names) {
      names.push_back(name);
    }
    for (const auto& name : module_filter_names) {
      names.push_back(name);
    }
    for (const absl::string_view name : names) {
      if (shadow->filterDisabled(name) != input->filterDisabled(name)) {
        mark(envoy_dynamic_module_type_route_specifier_compare_field_FilterDisabled);
        break;
      }
    }
  }

  if (const Envoy::Router::RouteEntry* shadow_entry = shadow->routeEntry();
      shadow_entry != nullptr) {
    const Envoy::Router::RouteEntry* input_entry = input->routeEntry();
    if (compares(envoy_dynamic_module_type_route_specifier_compare_field_ClusterName) &&
        shadow_entry->clusterName() != input_entry->clusterName()) {
      mark(envoy_dynamic_module_type_route_specifier_compare_field_ClusterName);
    }
    if (compares(envoy_dynamic_module_type_route_specifier_compare_field_Timeout) &&
        shadow_entry->timeout() != input_entry->timeout()) {
      mark(envoy_dynamic_module_type_route_specifier_compare_field_Timeout);
    }
    if (compares(envoy_dynamic_module_type_route_specifier_compare_field_IdleTimeout) &&
        shadow_entry->idleTimeout() != input_entry->idleTimeout()) {
      mark(envoy_dynamic_module_type_route_specifier_compare_field_IdleTimeout);
    }
    if (compares(envoy_dynamic_module_type_route_specifier_compare_field_MaxStreamDuration) &&
        shadow_entry->maxStreamDuration() != input_entry->maxStreamDuration()) {
      mark(envoy_dynamic_module_type_route_specifier_compare_field_MaxStreamDuration);
    }
    if (compares(envoy_dynamic_module_type_route_specifier_compare_field_Priority) &&
        shadow_entry->priority() != input_entry->priority()) {
      mark(envoy_dynamic_module_type_route_specifier_compare_field_Priority);
    }
    if (compares(envoy_dynamic_module_type_route_specifier_compare_field_RequestBodyBufferLimit) &&
        shadow_entry->requestBodyBufferLimit() != input_entry->requestBodyBufferLimit()) {
      mark(envoy_dynamic_module_type_route_specifier_compare_field_RequestBodyBufferLimit);
    }
    if (compares(
            envoy_dynamic_module_type_route_specifier_compare_field_ClusterNotFoundResponseCode) &&
        shadow_entry->clusterNotFoundResponseCode() != input_entry->clusterNotFoundResponseCode()) {
      mark(envoy_dynamic_module_type_route_specifier_compare_field_ClusterNotFoundResponseCode);
    }
    if (compares(envoy_dynamic_module_type_route_specifier_compare_field_RetryPolicy) &&
        !retryPoliciesEqual(*shadow_entry->retryPolicy(), *input_entry->retryPolicy())) {
      mark(envoy_dynamic_module_type_route_specifier_compare_field_RetryPolicy);
    }
    if (compares(envoy_dynamic_module_type_route_specifier_compare_field_HedgePolicy) &&
        (shadow_entry->hedgePolicy().initialRequests() !=
             input_entry->hedgePolicy().initialRequests() ||
         !Protobuf::util::MessageDifferencer::Equals(
             shadow_entry->hedgePolicy().additionalRequestChance(),
             input_entry->hedgePolicy().additionalRequestChance()) ||
         shadow_entry->hedgePolicy().hedgeOnPerTryTimeout() !=
             input_entry->hedgePolicy().hedgeOnPerTryTimeout())) {
      mark(envoy_dynamic_module_type_route_specifier_compare_field_HedgePolicy);
    }
    if (compares(envoy_dynamic_module_type_route_specifier_compare_field_MetadataMatch) &&
        !metadataMatchCriteriaEqual(shadow_entry->metadataMatchCriteria(),
                                    input_entry->metadataMatchCriteria())) {
      mark(envoy_dynamic_module_type_route_specifier_compare_field_MetadataMatch);
    }
    if (compares(envoy_dynamic_module_type_route_specifier_compare_field_HashPolicy)) {
      const Http::HashPolicy* shadow_policy = shadow_entry->hashPolicy();
      const Http::HashPolicy* input_policy = input_entry->hashPolicy();
      if ((shadow_policy == nullptr) != (input_policy == nullptr)) {
        mark(envoy_dynamic_module_type_route_specifier_compare_field_HashPolicy);
      } else if (shadow_policy != nullptr) {
        const HashResult shadow_hash =
            generateHash(*shadow_policy, context.headers, context.stream_info);
        const HashResult input_hash =
            generateHash(*input_policy, context.headers, context.stream_info);
        if (shadow_hash.hash != input_hash.hash || shadow_hash.cookies != input_hash.cookies) {
          mark(envoy_dynamic_module_type_route_specifier_compare_field_HashPolicy);
        }
      }
    }
    if (compares(envoy_dynamic_module_type_route_specifier_compare_field_RequestMirrorPolicies) &&
        !shadowPoliciesEqual(shadow_entry->shadowPolicies(), input_entry->shadowPolicies(),
                             context.headers, context.stream_info)) {
      mark(envoy_dynamic_module_type_route_specifier_compare_field_RequestMirrorPolicies);
    }
    if (compares(envoy_dynamic_module_type_route_specifier_compare_field_RequestPath) ||
        compares(envoy_dynamic_module_type_route_specifier_compare_field_RequestAuthority) ||
        compares(envoy_dynamic_module_type_route_specifier_compare_field_RequestHeaders)) {
      const FinalizedRequest shadow_request =
          finalizeRequest(*shadow, context.headers, context.stream_info);
      const FinalizedRequest input_request =
          finalizeRequest(*input, context.headers, context.stream_info);
      if (compares(envoy_dynamic_module_type_route_specifier_compare_field_RequestPath) &&
          (shadow_request.path != input_request.path ||
           shadow_request.original_path != input_request.original_path)) {
        mark(envoy_dynamic_module_type_route_specifier_compare_field_RequestPath);
      }
      if (compares(envoy_dynamic_module_type_route_specifier_compare_field_RequestAuthority) &&
          (shadow_request.authority != input_request.authority ||
           shadow_request.original_host != input_request.original_host ||
           shadow_request.forwarded_host != input_request.forwarded_host)) {
        mark(envoy_dynamic_module_type_route_specifier_compare_field_RequestAuthority);
      }
      if (compares(envoy_dynamic_module_type_route_specifier_compare_field_RequestHeaders) &&
          shadow_request.other_headers != input_request.other_headers) {
        mark(envoy_dynamic_module_type_route_specifier_compare_field_RequestHeaders);
      }
    }
    if (compares(envoy_dynamic_module_type_route_specifier_compare_field_ResponseHeaders) &&
        finalizeResponse(*shadow, context.headers, context.stream_info) !=
            finalizeResponse(*input, context.headers, context.stream_info)) {
      mark(envoy_dynamic_module_type_route_specifier_compare_field_ResponseHeaders);
    }
    if (compares(envoy_dynamic_module_type_route_specifier_compare_field_RateLimitPolicy) &&
        rateLimitSignatures(shadow_entry->rateLimitPolicy(), context.headers,
                            context.stream_info) !=
            rateLimitSignatures(input_entry->rateLimitPolicy(), context.headers,
                                context.stream_info)) {
      mark(envoy_dynamic_module_type_route_specifier_compare_field_RateLimitPolicy);
    }
    if (compares(envoy_dynamic_module_type_route_specifier_compare_field_Cors) &&
        corsSignature(shadow_entry->corsPolicy()) != corsSignature(input_entry->corsPolicy())) {
      mark(envoy_dynamic_module_type_route_specifier_compare_field_Cors);
    }
    return mismatches;
  }

  const Envoy::Router::DirectResponseEntry* shadow_direct = shadow->directResponseEntry();
  const Envoy::Router::DirectResponseEntry* input_direct = input->directResponseEntry();
  if (compares(envoy_dynamic_module_type_route_specifier_compare_field_ResponseCode) &&
      shadow_direct->responseCode() != input_direct->responseCode()) {
    mark(envoy_dynamic_module_type_route_specifier_compare_field_ResponseCode);
  }
  if (compares(envoy_dynamic_module_type_route_specifier_compare_field_RedirectLocation) &&
      shadow_direct->newUri(context.headers, context.stream_info) !=
          input_direct->newUri(context.headers, context.stream_info)) {
    mark(envoy_dynamic_module_type_route_specifier_compare_field_RedirectLocation);
  }
  if (compares(envoy_dynamic_module_type_route_specifier_compare_field_DirectResponseBody)) {
    // formatBody returns the body, writing into the scratch string when a formatter applies, so
    // the two return values are the bodies to compare.
    auto response = Http::ResponseHeaderMapImpl::create();
    std::string shadow_scratch;
    std::string input_scratch;
    const absl::string_view shadow_body =
        shadow_direct->formatBody(context.headers, *response, context.stream_info, shadow_scratch);
    const absl::string_view input_body =
        input_direct->formatBody(context.headers, *response, context.stream_info, input_scratch);
    if (shadow_direct->responseContentType() != input_direct->responseContentType() ||
        shadow_body != input_body) {
      mark(envoy_dynamic_module_type_route_specifier_compare_field_DirectResponseBody);
    }
  }
  return mismatches;
}

} // namespace DynamicModules
} // namespace RouteSpecifiers
} // namespace Extensions
} // namespace Envoy
