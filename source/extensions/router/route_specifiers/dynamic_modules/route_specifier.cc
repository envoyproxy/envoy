#include "source/extensions/router/route_specifiers/dynamic_modules/route_specifier.h"

#include <string>
#include <utility>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"
#include "source/common/config/well_known_names.h"
#include "source/common/http/hash_policy.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/router/retry_policy_impl.h"
#include "source/common/router/router_ratelimit.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace RouteSpecifiers {
namespace DynamicModules {
namespace {

using RouteKind = envoy_dynamic_module_type_route_specifier_route_kind;
using RouteOverrideProto =
    envoy::extensions::router::route_specifiers::dynamic_modules::v3::RouteOverride;

absl::StatusOr<RouteOverride>
buildRouteOverride(const RouteOverrideProto& proto_override,
                   Server::Configuration::ServerFactoryContext& context) {
  RouteOverride entry;
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
  // that replaces no property, which set_route_override would then accept as a decision.
  if (entry.retry_policy == nullptr && entry.metadata_match_criteria == nullptr &&
      entry.shadow_policies.empty() && entry.hash_policy == nullptr &&
      entry.hedge_policy == nullptr && entry.rate_limit_policy == nullptr &&
      entry.cors_policy == nullptr) {
    return absl::InvalidArgumentError("Route override must replace at least one property");
  }
  return entry;
}

std::optional<RuntimeFraction>
buildRuntimeFraction(const DynamicModuleRouteSpecifierProto& config) {
  if (!config.has_runtime_fraction()) {
    return std::nullopt;
  }
  return RuntimeFraction{config.runtime_fraction().runtime_key(),
                         config.runtime_fraction().default_value()};
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
         route_override != nullptr || path.has_value() || host.has_value() ||
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
      runtime_fraction_(buildRuntimeFraction(proto_config)),
      fail_closed_(proto_config.failure_policy() ==
                   envoy::extensions::router::route_specifiers::dynamic_modules::v3::NO_ROUTE),
      cluster_manager_(context.serverFactoryContext().clusterManager()),
      runtime_(context.serverFactoryContext().runtime()),
      time_source_(context.serverFactoryContext().timeSource()),
      stats_scope_(context.serverFactoryContext().serverScope().createScope(
          absl::StrCat(metrics_namespace, ".route_specifier.", proto_config.stat_prefix(), "."))),
      stats_{ALL_DYNAMIC_MODULE_ROUTE_SPECIFIER_STATS(POOL_COUNTER(*stats_scope_),
                                                      POOL_HISTOGRAM(*stats_scope_))} {}

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

const RouteOverride*
DynamicModuleRouteSpecifierConfig::routeOverride(absl::string_view override_id) const {
  const auto it = route_overrides_.find(override_id);
  return it != route_overrides_.end() ? &it->second : nullptr;
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

absl::StatusOr<DynamicModuleRouteSpecifierConfigSharedPtr>
newDynamicModuleRouteSpecifierConfig(const DynamicModuleRouteSpecifierProto& proto_config,
                                     Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                     Envoy::Router::RouteSpecifierFactoryContext& context) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  if (proto_config.failure_policy() == envoy::extensions::router::route_specifiers::
                                           dynamic_modules::v3::FAILURE_POLICY_UNSPECIFIED) {
    return absl::InvalidArgumentError("failure_policy must be set");
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

  config->route_overrides_.reserve(proto_config.route_overrides().size());
  for (const auto& proto_override : proto_config.route_overrides()) {
    const std::string& override_id = proto_override.override_id();
    if (config->route_overrides_.contains(override_id)) {
      return absl::InvalidArgumentError(
          fmt::format("duplicate route override id '{}'", override_id));
    }
    auto entry_or_error = buildRouteOverride(proto_override, context.serverFactoryContext());
    if (!entry_or_error.ok()) {
      return absl::InvalidArgumentError(
          fmt::format("route override '{}': {}", override_id, entry_or_error.status().message()));
    }
    if (validate_clusters) {
      for (const auto& shadow_policy : entry_or_error.value().shadow_policies) {
        // A policy that names its cluster through a request header resolves it per request, so only
        // a statically named cluster can be checked here.
        if (!shadow_policy->cluster().empty() &&
            !context.serverFactoryContext().clusterManager().hasCluster(shadow_policy->cluster())) {
          return absl::InvalidArgumentError(
              fmt::format("route override '{}': unknown shadow cluster '{}'", override_id,
                          shadow_policy->cluster()));
        }
      }
    }
    config->route_overrides_.emplace(override_id, std::move(entry_or_error.value()));
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
  const RouteOverride* entry = overrides_.route_override;
  return entry != nullptr && entry->retry_policy != nullptr ? entry->retry_policy
                                                            : DelegatingRouteEntry::retryPolicy();
}

const Envoy::Router::MetadataMatchCriteria* DynamicModuleRouteEntry::metadataMatchCriteria() const {
  const RouteOverride* entry = overrides_.route_override;
  return entry != nullptr && entry->metadata_match_criteria != nullptr
             ? entry->metadata_match_criteria.get()
             : DelegatingRouteEntry::metadataMatchCriteria();
}

const std::vector<Envoy::Router::ShadowPolicyPtr>& DynamicModuleRouteEntry::shadowPolicies() const {
  const RouteOverride* entry = overrides_.route_override;
  return entry != nullptr && !entry->shadow_policies.empty()
             ? entry->shadow_policies
             : DelegatingRouteEntry::shadowPolicies();
}

const Http::HashPolicy* DynamicModuleRouteEntry::hashPolicy() const {
  const RouteOverride* entry = overrides_.route_override;
  return entry != nullptr && entry->hash_policy != nullptr ? entry->hash_policy.get()
                                                           : DelegatingRouteEntry::hashPolicy();
}

const Envoy::Router::HedgePolicy& DynamicModuleRouteEntry::hedgePolicy() const {
  const RouteOverride* entry = overrides_.route_override;
  return entry != nullptr && entry->hedge_policy != nullptr ? *entry->hedge_policy
                                                            : DelegatingRouteEntry::hedgePolicy();
}

const Envoy::Router::RateLimitPolicy& DynamicModuleRouteEntry::rateLimitPolicy() const {
  const RouteOverride* entry = overrides_.route_override;
  return entry != nullptr && entry->rate_limit_policy != nullptr
             ? *entry->rate_limit_policy
             : DelegatingRouteEntry::rateLimitPolicy();
}

const Envoy::Router::CorsPolicy* DynamicModuleRouteEntry::corsPolicy() const {
  const RouteOverride* entry = overrides_.route_override;
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

  Decision result = resolve(context, decision);

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
    case Failure::ModuleError:
      config_->stats().failure_module_error_.inc();
      break;
    case Failure::TemplateNotSelected:
      config_->stats().failure_template_not_selected_.inc();
      break;
    case Failure::TemplateMatchFailed:
      config_->stats().failure_template_match_failed_.inc();
      break;
    case Failure::OverrideWithoutRoute:
      config_->stats().failure_override_without_route_.inc();
      break;
    case Failure::OverrideOnNonRouteEntry:
      config_->stats().failure_override_on_non_route_entry_.inc();
      break;
    case Failure::RouteMetadata:
      config_->stats().failure_route_metadata_.inc();
      break;
    case Failure::None:
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
    return fail(Failure::ModuleError);
  case envoy_dynamic_module_type_route_specifier_decision_Override: {
    config_->stats().decision_override_.inc();
    if (context.input_route == nullptr) {
      return fail(Failure::OverrideWithoutRoute);
    }
    Decision wrapped = wrap(context.input_route, context, status(Status::Continue));
    return wrapped.failure == Failure::None ? wrapped : fail(wrapped.failure);
  }
  case envoy_dynamic_module_type_route_specifier_decision_SelectTemplate: {
    config_->stats().decision_select_template_.inc();
    if (context.selected_template == nullptr) {
      return fail(Failure::TemplateNotSelected);
    }
    // set_template evaluated the template against the request, so reuse the result rather than
    // matching a second time. A null result means the match did not hold for the request.
    if (context.selected_route == nullptr) {
      return fail(Failure::TemplateMatchFailed);
    }
    Decision wrapped = wrap(context.selected_route, context, status(Status::StopIteration));
    return wrapped.failure == Failure::None ? wrapped : fail(wrapped.failure);
  }
  }
  // A module built against a newer ABI could return a decision this build does not know.
  config_->stats().decision_error_.inc();
  return fail(Failure::ModuleError);
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
    return {nullptr, status, Failure::OverrideOnNonRouteEntry};
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
    decision.failure = Failure::RouteMetadata;
  });
  return decision;
}

} // namespace DynamicModules
} // namespace RouteSpecifiers
} // namespace Extensions
} // namespace Envoy
