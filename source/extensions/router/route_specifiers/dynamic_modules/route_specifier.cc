#include "source/extensions/router/route_specifiers/dynamic_modules/route_specifier.h"

#include <string>
#include <tuple>
#include <utility>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/regex.h"
#include "source/common/common/thread.h"
#include "source/common/config/well_known_names.h"
#include "source/common/http/hash_policy.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/path_utility.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/router/path_rewrite_utility.h"
#include "source/common/router/retry_policy_impl.h"
#include "source/common/router/router_ratelimit.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace RouteSpecifiers {
namespace DynamicModules {
namespace {

using RouteKind = envoy_dynamic_module_type_route_specifier_route_kind;
using RouteOverrideProto =
    envoy::extensions::router::route_specifiers::dynamic_modules::v3::RouteOverride;

// Whether every match of a route template returns the same route, which the built route cache
// requires. Weighted clusters, a cluster header and a cluster specifier plugin resolve the cluster
// per request, and so does any way to name a cluster added later.
bool matchesTheSameRoute(const envoy::config::route::v3::Route& route) {
  return !route.has_route() ||
         route.route().cluster_specifier_case() == envoy::config::route::v3::RouteAction::kCluster;
}

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
  if (proto_override.has_regex_rewrite()) {
    auto regex_or_error =
        Regex::Utility::parseRegex(proto_override.regex_rewrite().pattern(), context.regexEngine());
    RETURN_IF_NOT_OK_REF(regex_or_error.status());
    entry.regex_rewrite = std::move(regex_or_error.value());
    entry.regex_rewrite_substitution = proto_override.regex_rewrite().substitution();
  }
  if (proto_override.has_tracing()) {
    auto tracing_or_error = Envoy::Router::RouteTracingImpl::create(proto_override.tracing());
    RETURN_IF_NOT_OK_REF(tracing_or_error.status());
    entry.tracing = std::move(tracing_or_error.value());
  }
  entry.metadata = proto_override.metadata();
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
  const bool has_metadata =
      !entry.metadata.filter_metadata().empty() || !entry.metadata.typed_filter_metadata().empty();
  if (entry.retry_policy == nullptr && entry.metadata_match_criteria == nullptr &&
      entry.shadow_policies.empty() && entry.hash_policy == nullptr &&
      entry.hedge_policy == nullptr && entry.rate_limit_policy == nullptr &&
      entry.cors_policy == nullptr && entry.regex_rewrite == nullptr && entry.tracing == nullptr &&
      !has_metadata) {
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

// Layers the metadata a module recorded onto the metadata of the route and validates it. Returns a
// null pack when the module recorded none, so that both metadata accessors fall back to the route,
// and an error when a typed metadata factory rejects the merged metadata.
absl::StatusOr<Envoy::Router::RouteMetadataPackPtr>
buildMetadataPack(const Envoy::Router::Route& route,
                  const envoy::config::core::v3::Metadata& overrides) {
  if (overrides.filter_metadata().empty() && overrides.typed_filter_metadata().empty()) {
    return Envoy::Router::RouteMetadataPackPtr{};
  }
  // One copy of the route metadata, merged in place and then moved into the pack.
  envoy::config::core::v3::Metadata merged = route.metadata();
  // Merge per namespace so an entry replaces only its own key while the other keys of the route
  // stay in effect. A top-level merge would replace the whole namespace instead.
  for (const auto& [name, fields] : overrides.filter_metadata()) {
    (*merged.mutable_filter_metadata())[name].MergeFrom(fields);
  }
  for (const auto& [name, typed] : overrides.typed_filter_metadata()) {
    (*merged.mutable_typed_filter_metadata())[name] = typed;
  }
  // create() runs the registered typed metadata factories once and returns an error instead of
  // throwing, so the wrapper it feeds cannot throw when it is constructed.
  return Envoy::Router::RouteMetadataPack::create(std::move(merged));
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

// Applies what the selected route override carries onto the recorded overrides before the route
// is built. Deferred to resolution so that unset_route_override can revert the selection cleanly,
// and defined so that the values the module records itself win whatever the call order: the
// override's regex rewrite applies only when the module recorded no path, and its metadata sits
// under the metadata the module recorded.
void applySelectedRouteOverride(RouteSpecifierContext& context) {
  const RouteOverride* entry = context.overrides.route_override;
  if (entry == nullptr) {
    return;
  }
  if (entry->regex_rewrite != nullptr && !context.overrides.path.has_value()) {
    if (context.overrides.prefix_rewrite_rule.has_value() ||
        context.built_route_lookup.has_value()) {
      // A rule applies when the request headers are finalized, with the regex rewrite as its
      // fallback then, and a route the built route cache may hold serves later requests.
      context.overrides.finalize_regex_rewrite = true;
    } else {
      // A result over the bound or an empty rewrite leaves the path alone, the same way
      // set_prefix_rewrite does.
      std::optional<std::string> rewritten = Envoy::Router::rewritePathByPrefixOrRegex(
          context.headers.getPathValue(), "", "", entry->regex_rewrite.get(),
          entry->regex_rewrite_substitution, context.config.maxRewrittenPathBytes());
      if (rewritten.has_value()) {
        context.overrides.path = std::move(*rewritten);
      }
    }
  }
  for (const auto& [name, fields] : entry->metadata.filter_metadata()) {
    auto& slot = (*context.overrides.route_metadata.mutable_filter_metadata())[name];
    Protobuf::Struct merged = fields;
    merged.MergeFrom(slot);
    slot = std::move(merged);
  }
  for (const auto& [name, typed] : entry->metadata.typed_filter_metadata()) {
    context.overrides.route_metadata.mutable_typed_filter_metadata()->insert({name, typed});
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

// The part of the path a rule replaces. A literal span carries no '?' or '#', so it ends before the
// query string and fragment, and the whole path excludes them like the part a regex route matches.
absl::string_view replacedSpan(const PrefixRewriteRule& rule, absl::string_view path) {
  return rule.matched.has_value() ? absl::string_view(*rule.matched)
                                  : Http::PathUtil::removeQueryAndFragment(path);
}

} // namespace

std::optional<std::string> PrefixRewriteRule::apply(absl::string_view path,
                                                    size_t max_bytes) const {
  return Envoy::Router::rewritePathByPrefixOrRegex(path, replacedSpan(*this, path), replacement,
                                                   nullptr, "", max_bytes);
}

bool PrefixRewriteRule::appliesTo(absl::string_view path, size_t max_bytes) const {
  const absl::string_view span = replacedSpan(*this, path);
  // The checks rewritePathByPrefixOrRegex makes for a prefix rewrite, without building the path.
  return !replacement.empty() && absl::StartsWithIgnoreCase(path, span) &&
         replacement.size() <= max_bytes &&
         path.size() - span.size() <= max_bytes - replacement.size();
}

bool RouteOverrides::hasRouteEntryOverrides() const {
  return !cluster_name.empty() || timeout.has_value() || idle_timeout.has_value() ||
         max_stream_duration.has_value() || request_body_buffer_limit.has_value() ||
         priority.has_value() || cluster_not_found_response_code.has_value() ||
         (route_override != nullptr && route_override->hasRouteEntryProperties()) ||
         path.has_value() || prefix_rewrite_rule.has_value() || host.has_value() ||
         !request_headers_to_add.empty() || !request_headers_to_remove.empty() ||
         !response_headers_to_add.empty() || !response_headers_to_remove.empty();
}

bool RouteOverrides::operator==(const RouteOverrides& other) const {
  const auto fields = [](const RouteOverrides& overrides) {
    return std::tie(overrides.cluster_name, overrides.timeout, overrides.idle_timeout,
                    overrides.max_stream_duration, overrides.request_body_buffer_limit,
                    overrides.priority, overrides.cluster_not_found_response_code,
                    overrides.route_override, overrides.filter_disabled, overrides.route_name,
                    overrides.path, overrides.prefix_rewrite_rule, overrides.finalize_regex_rewrite,
                    overrides.host, overrides.request_headers_to_add,
                    overrides.request_headers_to_remove, overrides.response_headers_to_add,
                    overrides.response_headers_to_remove);
  };
  return fields(*this) == fields(other) && MessageUtil()(route_metadata, other.route_metadata);
}

bool RouteOverrides::hasRouteOverrides() const {
  return !route_metadata.filter_metadata().empty() ||
         !route_metadata.typed_filter_metadata().empty() || !filter_disabled.empty() ||
         (route_override != nullptr && route_override->tracing != nullptr) ||
         route_name.has_value();
}

DynamicModuleRouteSpecifierConfig::DynamicModuleRouteSpecifierConfig(
    const DynamicModuleRouteSpecifierProto& proto_config, absl::string_view specifier_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Envoy::Router::RouteSpecifierFactoryContext& context, absl::string_view metrics_namespace)
    : metrics_scope_(context.serverFactoryContext().serverScope().createScope(
          absl::StrCat(metrics_namespace, "."))),
      metrics_(*metrics_scope_), dynamic_module_(std::move(dynamic_module)),
      specifier_name_(proto_config.specifier_name()), specifier_config_(specifier_config),
      specifier_instance_id_(proto_config.specifier_instance_id()),
      runtime_fraction_(buildRuntimeFraction(proto_config)),
      max_rewritten_path_bytes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, max_rewritten_path_bytes, 65536)),
      has_built_route_cache_(proto_config.has_built_route_cache()),
      fail_closed_(proto_config.failure_policy() ==
                   envoy::extensions::router::route_specifiers::dynamic_modules::v3::NO_ROUTE),
      continue_matching_on_failure_(
          proto_config.failure_policy() ==
          envoy::extensions::router::route_specifiers::dynamic_modules::v3::CONTINUE_MATCHING),
      cluster_manager_(context.serverFactoryContext().clusterManager()),
      runtime_(context.serverFactoryContext().runtime()),
      time_source_(context.serverFactoryContext().timeSource()),
      stats_scope_(context.serverFactoryContext().serverScope().createScope(
          absl::StrCat(metrics_namespace, ".route_specifier.", proto_config.stat_prefix(), "."))),
      stats_{ALL_DYNAMIC_MODULE_ROUTE_SPECIFIER_STATS(
          POOL_COUNTER(*stats_scope_), POOL_GAUGE(*stats_scope_), POOL_HISTOGRAM(*stats_scope_))} {}

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
  templates_.emplace(std::string(id), Template{std::string(id), std::move(built.value()), kind,
                                               matchesTheSameRoute(route)});
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
  // Only routes built from templates are cached, and templates need a route builder.
  if (proto_config.has_built_route_cache() && !context.routeBuilder().has_value()) {
    return absl::InvalidArgumentError(
        "built_route_cache requires a route specifier configured on a virtual host or a route");
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

  // The route destroy and user data discard hooks are optional, so a module built with an older
  // SDK that does not export them is accepted and each hook is a no op.
  auto on_route_destroy = dynamic_module->getFunctionPointer<OnRouteSpecifierRouteDestroyType>(
      "envoy_dynamic_module_on_route_specifier_route_destroy");
  auto on_user_data_discard =
      dynamic_module->getFunctionPointer<OnRouteSpecifierUserDataDiscardType>(
          "envoy_dynamic_module_on_route_specifier_user_data_discard");

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
  config->on_route_destroy_ = on_route_destroy.ok() ? on_route_destroy.value() : nullptr;
  config->on_user_data_discard_ =
      on_user_data_discard.ok() ? on_user_data_discard.value() : nullptr;

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
        id, DynamicModuleRouteSpecifierConfig::Template{
                id, std::move(built.value()), kind, matchesTheSameRoute(route_template.route())});
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

namespace {

// A worker sweeps its built route cache every quarter of the entry age and evicts the entries idle
// for more sweeps than this, which is between the entry age and 1.25 times it after their last use.
constexpr uint32_t SweepsPerEntryAge = 4;

// Whether a cached route serves a request for the path. A module only records a rule for a path it
// applies to, so a route that carries one serves only those paths.
bool servesPath(const CachedRoute& cached, absl::string_view path, size_t max_bytes) {
  return cached.recorded == nullptr || !cached.recorded->prefix_rewrite_rule.has_value() ||
         cached.recorded->prefix_rewrite_rule->appliesTo(path, max_bytes);
}

// Whether a decision recorded the route the cached route was built from.
bool recordsTheCachedRoute(const CachedRoute& cached, const RouteOverrides* recorded,
                           const std::optional<uint64_t>& user_data) {
  if (cached.user_data != user_data) {
    return false;
  }
  // A route built without a wrapper was recorded with nothing that needs one.
  if (cached.recorded == nullptr || recorded == nullptr) {
    return cached.recorded == recorded;
  }
  return *cached.recorded == *recorded;
}

} // namespace

BuiltRouteCache::BuiltRouteCache(Event::Dispatcher& dispatcher,
                                 DynamicModuleRouteSpecifierConfigSharedPtr config,
                                 const BuiltRouteCacheSettings& settings,
                                 Random::RandomGenerator& random)
    : config_(std::move(config)), settings_(settings), random_(random),
      sweep_timer_(dispatcher.createTimer([this]() { sweep(); })) {}

BuiltRouteCache::~BuiltRouteCache() {
  config_->stats().built_route_cache_entries_.sub(entries_.size());
}

const CachedRoute* BuiltRouteCache::lookup(uint64_t key, const Envoy::Router::Route* base,
                                           absl::string_view path) {
  const auto it = index_.find(Key{key, base});
  if (it == index_.end() ||
      !servesPath(entries_[it->second].value, path, config_->maxRewrittenPathBytes())) {
    config_->stats().built_route_cache_miss_.inc();
    return nullptr;
  }
  Entry& entry = entries_[it->second];
  entry.last_used_tick = tick_;
  entry.referenced = true;
  config_->stats().built_route_cache_hit_.inc();
  return &entry.value;
}

void BuiltRouteCache::insert(uint64_t key, const Envoy::Router::Route* base, CachedRoute route) {
  const Key entry_key{key, base};
  // A route cached for the key stays, also when its rule does not apply to the path of the miss.
  if (index_.contains(entry_key)) {
    return;
  }
  Entry entry{entry_key, std::move(route), tick_, true};
  if (entries_.size() < settings_.max_entries) {
    index_.emplace(entry_key, entries_.size());
    entries_.push_back(std::move(entry));
    config_->stats().built_route_cache_entries_.inc();
    if (!sweep_timer_->enabled()) {
      sweep_timer_->enableTimer(settings_.sweep_interval);
    }
    return;
  }
  const size_t victim = clockVictim();
  index_.erase(entries_[victim].key);
  index_.emplace(entry_key, victim);
  entries_[victim] = std::move(entry);
  config_->stats().built_route_cache_evict_.inc();
}

void BuiltRouteCache::erase(uint64_t key, const Envoy::Router::Route* base) {
  const auto it = index_.find(Key{key, base});
  if (it != index_.end()) {
    removeAt(it->second);
  }
}

bool BuiltRouteCache::sampleVerification() {
  return settings_.verify_one_in != 0 && random_.random() % settings_.verify_one_in == 0;
}

size_t BuiltRouteCache::clockVictim() {
  const auto advance = [this]() {
    if (++hand_ == entries_.size()) {
      hand_ = 0;
    }
  };
  while (entries_[hand_].referenced) {
    entries_[hand_].referenced = false;
    advance();
  }
  const size_t victim = hand_;
  advance();
  return victim;
}

void BuiltRouteCache::sweep() {
  ++tick_;
  // Walking down lets removeAt move the last entry into a slot that was already visited.
  for (size_t i = entries_.size(); i > 0; --i) {
    if (tick_ - entries_[i - 1].last_used_tick > SweepsPerEntryAge) {
      removeAt(i - 1);
      config_->stats().built_route_cache_evict_.inc();
    }
  }
  if (!entries_.empty()) {
    sweep_timer_->enableTimer(settings_.sweep_interval);
  }
}

void BuiltRouteCache::removeAt(size_t index) {
  index_.erase(entries_[index].key);
  if (index + 1 != entries_.size()) {
    entries_[index] = std::move(entries_.back());
    index_[entries_[index].key] = index;
  }
  entries_.pop_back();
  config_->stats().built_route_cache_entries_.dec();
}

DynamicModuleRoute::DynamicModuleRoute(
    Envoy::Router::RouteConstSharedPtr route, DynamicModuleRouteSpecifierConfigSharedPtr config,
    RouteOverrides&& overrides,
    Envoy::Config::MetadataPackPtr<Envoy::Router::HttpRouteTypedMetadataFactory> metadata_pack,
    std::optional<uint64_t> user_data)
    : Envoy::Router::DelegatingRoute(std::move(route)), config_(std::move(config)),
      overrides_(std::move(overrides)), metadata_pack_(std::move(metadata_pack)),
      user_data_(user_data) {}

DynamicModuleRoute::~DynamicModuleRoute() {
  if (user_data_.has_value() && config_->onRouteDestroy() != nullptr) {
    config_->stats().route_destroy_.inc();
    config_->onRouteDestroy()(config_->in_module_config_, *user_data_);
  }
}

const std::string& DynamicModuleRoute::routeName() const {
  return overrides_.route_name.has_value() ? *overrides_.route_name
                                           : Envoy::Router::DelegatingRoute::routeName();
}

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

const Envoy::Router::RouteTracing* DynamicModuleRoute::tracingConfig() const {
  const RouteOverride* entry = overrides_.route_override;
  return entry != nullptr && entry->tracing != nullptr
             ? entry->tracing.get()
             : Envoy::Router::DelegatingRoute::tracingConfig();
}

DynamicModuleRouteEntry::DynamicModuleRouteEntry(
    Envoy::Router::RouteConstSharedPtr route, DynamicModuleRouteSpecifierConfigSharedPtr config,
    RouteOverrides&& overrides,
    Envoy::Config::MetadataPackPtr<Envoy::Router::HttpRouteTypedMetadataFactory> metadata_pack,
    std::optional<uint64_t> user_data)
    : DelegatingRouteEntry(std::move(route)), config_(std::move(config)),
      overrides_(std::move(overrides)), metadata_pack_(std::move(metadata_pack)),
      user_data_(user_data) {}

DynamicModuleRouteEntry::~DynamicModuleRouteEntry() {
  if (user_data_.has_value() && config_->onRouteDestroy() != nullptr) {
    config_->stats().route_destroy_.inc();
    config_->onRouteDestroy()(config_->in_module_config_, *user_data_);
  }
}

const std::string& DynamicModuleRouteEntry::routeName() const {
  return overrides_.route_name.has_value() ? *overrides_.route_name
                                           : DelegatingRouteEntry::routeName();
}

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

const Envoy::Router::RouteTracing* DynamicModuleRouteEntry::tracingConfig() const {
  const RouteOverride* entry = overrides_.route_override;
  return entry != nullptr && entry->tracing != nullptr ? entry->tracing.get()
                                                       : DelegatingRouteEntry::tracingConfig();
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

bool DynamicModuleRouteEntry::hasFinalizationRewrite() const {
  return overrides_.prefix_rewrite_rule.has_value() || overrides_.finalize_regex_rewrite;
}

std::optional<std::string>
DynamicModuleRouteEntry::finalizationRewrite(absl::string_view path) const {
  const size_t max_bytes = config_->maxRewrittenPathBytes();
  std::optional<std::string> rewritten;
  if (overrides_.prefix_rewrite_rule.has_value()) {
    rewritten = overrides_.prefix_rewrite_rule->apply(path, max_bytes);
  }
  if (!rewritten.has_value() && overrides_.finalize_regex_rewrite) {
    const RouteOverride& entry = *overrides_.route_override;
    rewritten = Envoy::Router::rewritePathByPrefixOrRegex(
        path, "", "", entry.regex_rewrite.get(), entry.regex_rewrite_substitution, max_bytes);
  }
  return rewritten;
}

std::string DynamicModuleRouteEntry::currentUrlPathAfterRewrite(
    const Http::RequestHeaderMap& headers, const Formatter::Context& context,
    const StreamInfo::StreamInfo& stream_info) const {
  if (overrides_.path.has_value()) {
    return *overrides_.path;
  }
  std::optional<std::string> rewritten = finalizationRewrite(headers.getPathValue());
  return rewritten.has_value()
             ? std::move(*rewritten)
             : DelegatingRouteEntry::currentUrlPathAfterRewrite(headers, context, stream_info);
}

void DynamicModuleRouteEntry::finalizeRequestHeaders(Http::RequestHeaderMap& headers,
                                                     const Formatter::Context& context,
                                                     const StreamInfo::StreamInfo& stream_info,
                                                     bool insert_envoy_original_path) const {
  // A finalization rewrite applies to the path before the rewrites of the route, which stay in
  // effect when it yields nothing. A recorded path takes precedence over both.
  std::optional<std::string> rewritten;
  std::string original_path;
  if (!overrides_.path.has_value() && hasFinalizationRewrite()) {
    rewritten = finalizationRewrite(headers.getPathValue());
    if (!rewritten.has_value()) {
      config_->stats().rewrite_not_applied_.inc();
    } else if (insert_envoy_original_path) {
      original_path = std::string(headers.getPathValue());
    }
  }
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
  } else if (rewritten.has_value()) {
    if (insert_envoy_original_path) {
      headers.setEnvoyOriginalPath(original_path);
    }
    headers.setPath(*rewritten);
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

DynamicModuleRouteSpecifier::DynamicModuleRouteSpecifier(
    DynamicModuleRouteSpecifierConfigSharedPtr config,
    const DynamicModuleRouteSpecifierProto& proto_config, ThreadLocal::SlotAllocator& tls,
    Random::RandomGenerator& random)
    : config_(std::move(config)) {
  if (!proto_config.has_built_route_cache()) {
    return;
  }
  const auto& cache = proto_config.built_route_cache();
  const BuiltRouteCacheSettings settings{
      cache.max_entries_per_worker(),
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(cache, max_idle_time, 600000)) /
          SweepsPerEntryAge,
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(cache, verify_one_in, 1024)};
  built_route_cache_ = ThreadLocal::TypedSlot<BuiltRouteCache>::makeUnique(tls);
  built_route_cache_->set([config = config_, settings, &random](Event::Dispatcher& dispatcher) {
    return std::make_shared<BuiltRouteCache>(dispatcher, config, settings, random);
  });
}

Envoy::Router::OnRouteResult
DynamicModuleRouteSpecifier::onRoute(Envoy::Router::RouteConstSharedPtr route,
                                     const Http::RequestHeaderMap& headers,
                                     const StreamInfo::StreamInfo& stream_info, uint64_t random,
                                     Envoy::Router::OnRouteInputStatus) const {
  const auto& runtime_fraction = config_->runtimeFraction();
  if (runtime_fraction.has_value() &&
      !config_->runtime().snapshot().featureEnabled(runtime_fraction->key,
                                                    runtime_fraction->default_value, random)) {
    config_->stats().runtime_skipped_.inc();
    return {std::move(route)};
  }

  const MonotonicTime start = config_->timeSource().monotonicTime();
  const auto record_duration = [start](Stats::Histogram& histogram, MonotonicTime end) {
    histogram.recordValue(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  };
  RouteSpecifierContext context{*config_, route, headers, stream_info, random};
  context.built_route_cache_slot = built_route_cache_.get();
  // The module can return a status this build does not know, for example from a newer ABI, so it
  // is read as its underlying integer. Loading an out of range enum value is undefined behavior.
  const uint32_t on_route_status = static_cast<uint32_t>(
      config_->on_route_(config_->in_module_config_, static_cast<void*>(&context)));
  // Reading the clock is the most expensive thing this method does that is not the module itself,
  // so the start of the specifier doubles as the start of the module, with only the context
  // construction between the two.
  const MonotonicTime module_end = config_->timeSource().monotonicTime();
  record_duration(config_->stats().on_route_duration_, module_end);

  Decision result = resolve(context, on_route_status);
  // User data no route carries never reaches the route destroy hook, so it is handed back here.
  const bool discard = context.user_data.has_value() && result.recorded == nullptr &&
                       config_->onUserDataDiscard() != nullptr;
  if (discard) {
    config_->stats().user_data_discard_.inc();
    config_->onUserDataDiscard()(config_->in_module_config_, *context.user_data);
  }

  // A decision reads the clock again when it builds a route, fails or hands its user data back, or
  // when its lookup in the built route cache misses or is verified. Any other decision ends with
  // the module, since only a few branches follow it.
  const auto& lookup = context.built_route_lookup;
  const bool ends_with_module =
      !discard && result.recorded == nullptr && result.failure == Failure::None &&
      (!lookup.has_value() || (lookup->hit.has_value() && !lookup->verify));
  record_duration(config_->stats().specifier_duration_,
                  ends_with_module ? module_end : config_->timeSource().monotonicTime());
  return {std::move(result.route), result.status};
}

DynamicModuleRouteSpecifier::Decision
DynamicModuleRouteSpecifier::resolve(RouteSpecifierContext& context,
                                     uint32_t on_route_status) const {
  using Status = Envoy::Router::OnRouteStatus;
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
    case Failure::ReusePreviousRejected:
      config_->stats().reuse_previous_rejected_.inc();
      break;
    case Failure::None:
      IS_ENVOY_BUG("route specifier failure without a reason");
      break;
    }
    ENVOY_LOG(debug, "dynamic module route specifier could not honor the decision, reason {}",
              static_cast<int>(failure));
    if (config_->continueMatchingOnFailure()) {
      return Decision{context.input_route, Status::StopIterationAndSkipRoute, failure};
    }
    if (config_->failClosed()) {
      return Decision{nullptr, Status::StopIteration, failure};
    }
    return Decision{context.input_route, Status::Continue, failure};
  };

  Status status;
  switch (on_route_status) {
  case envoy_dynamic_module_type_route_specifier_on_route_status_Continue:
    status = Status::Continue;
    break;
  case envoy_dynamic_module_type_route_specifier_on_route_status_StopIteration:
    status = Status::StopIteration;
    break;
  case envoy_dynamic_module_type_route_specifier_on_route_status_StopIterationAndSkipRoute:
    // The skip turns the route down, so nothing is built for the request: the recorded decision,
    // template and overrides are ignored.
    config_->stats().route_skipped_.inc();
    return {context.input_route, Status::StopIterationAndSkipRoute};
  default:
    // A module built against a newer ABI could return a status this build does not know. This is
    // not a decision the module recorded, so it keeps its own counter.
    config_->stats().unknown_status_.inc();
    return fail(Failure::ModuleError);
  }

  switch (context.decision) {
  case envoy_dynamic_module_type_route_specifier_decision_PassThrough:
    config_->stats().decision_pass_through_.inc();
    return {context.input_route, status};
  case envoy_dynamic_module_type_route_specifier_decision_NoRoute:
    config_->stats().decision_no_route_.inc();
    return {nullptr, status};
  case envoy_dynamic_module_type_route_specifier_decision_Error:
    config_->stats().decision_error_.inc();
    return fail(Failure::ModuleError);
  case envoy_dynamic_module_type_route_specifier_decision_Unspecified: {
    if (context.selected_template != nullptr) {
      // The two ways the default decision generates its route keep their own statistic, so
      // selecting a template stays distinguishable from refining the resolved route.
      config_->stats().decision_has_template_.inc();
      // set_route_template evaluated the template against the request, so reuse the result rather
      // than matching a second time. A null result means the match did not hold for the request.
      if (context.selected_route == nullptr) {
        return fail(Failure::TemplateMatchFailed);
      }
      BuiltRouteLookup* lookup =
          context.built_route_lookup.has_value() ? &*context.built_route_lookup : nullptr;
      // A hit serves the cached route, so what the module recorded is not applied.
      if (lookup != nullptr && lookup->hit.has_value() && !lookup->verify) {
        return {std::move(lookup->hit->route), status};
      }
      applySelectedRouteOverride(context);
      Decision wrapped = wrap(context.selected_route, context, status);
      if (lookup != nullptr && lookup->hit.has_value()) {
        // A verified hit serves the route the module recorded, which keeps the user data the
        // module records on a route of its own whenever the recording builds one.
        verifyCachedRoute(context, wrapped, *lookup);
      } else if (lookup != nullptr && recordsACacheableRoute(context, wrapped)) {
        // Even the template cached as it is spares the module its recording on a hit.
        lookup->cache->insert(lookup->key, context.selected_route.get(),
                              {wrapped.route, wrapped.recorded, context.user_data});
      }
      if (wrapped.failure != Failure::None) {
        return fail(wrapped.failure);
      }
      return wrapped;
    }
    if (context.template_selection_failed) {
      // A selection that failed must not silently fall back to the route the specifier was
      // given: the module asked for a template the configuration does not declare. A module
      // that wants to probe without committing checks the configuration getters instead.
      return fail(Failure::TemplateNotSelected);
    }
    config_->stats().decision_has_override_.inc();
    applySelectedRouteOverride(context);
    if (context.input_route == nullptr) {
      // With nothing recorded the default decision passes the route through unchanged, which is
      // what makes it safe when the module records none: the run with no route stays a run with
      // no route. Anything recorded needs a route to be applied to.
      if (context.overrides.hasRouteEntryOverrides() || context.overrides.hasRouteOverrides() ||
          context.user_data.has_value()) {
        return fail(Failure::OverrideWithoutRoute);
      }
      return {nullptr, status};
    }
    Decision wrapped = wrap(context.input_route, context, status);
    return wrapped.failure == Failure::None ? wrapped : fail(wrapped.failure);
  }
  case envoy_dynamic_module_type_route_specifier_decision_ReusePrevious: {
    config_->stats().decision_reuse_previous_.inc();
    // Reuse the route the connection manager last installed. No new wrapper is allocated, so the
    // destroy hook does not fire again and the previous route keeps its user data until the stream
    // releases it.
    Envoy::Router::RouteConstSharedPtr previous = context.stream_info.routeSharedPtr();
    if (previous == nullptr) {
      return fail(Failure::ReusePreviousRejected);
    }
    return {std::move(previous), status};
  }
  case envoy_dynamic_module_type_route_specifier_decision_Sentinel:
    // Not a decision a module records, handled like any other value this build does not know.
    break;
  }
  // A module built against a newer ABI could record a decision this build does not know.
  config_->stats().decision_error_.inc();
  return fail(Failure::ModuleError);
}

DynamicModuleRouteSpecifier::Decision
DynamicModuleRouteSpecifier::wrap(Envoy::Router::RouteConstSharedPtr route,
                                  RouteSpecifierContext& context,
                                  Envoy::Router::OnRouteStatus status) const {
  const bool route_entry_overrides = context.overrides.hasRouteEntryOverrides();
  // User data forces a wrapper even with no override, so the route destroy hook fires for the
  // produced route.
  if (!route_entry_overrides && !context.overrides.hasRouteOverrides() &&
      !context.user_data.has_value()) {
    return {std::move(route), status};
  }
  if (route_entry_overrides && route->routeEntry() == nullptr) {
    return {nullptr, status, Failure::OverrideOnNonRouteEntry};
  }
  // Build and validate the metadata pack once, before the wrapper is constructed, so a namespace a
  // typed metadata factory rejects becomes a decision failure rather than an exception the wrapper
  // constructor throws.
  auto metadata_pack = buildMetadataPack(*route, context.overrides.route_metadata);
  if (!metadata_pack.ok()) {
    ENVOY_LOG_EVERY_POW_2(warn, "dynamic module route metadata was rejected: {}",
                          metadata_pack.status().message());
    return {nullptr, status, Failure::RouteMetadata};
  }
  Decision decision{nullptr, status};
  if (route_entry_overrides) {
    auto entry = std::make_shared<DynamicModuleRouteEntry>(
        std::move(route), config_, std::move(context.overrides), std::move(metadata_pack.value()),
        context.user_data);
    decision.recorded = &entry->recordedOverrides();
    decision.route = std::move(entry);
  } else {
    auto wrapper = std::make_shared<DynamicModuleRoute>(
        std::move(route), config_, std::move(context.overrides), std::move(metadata_pack.value()),
        context.user_data);
    decision.recorded = &wrapper->recordedOverrides();
    decision.route = std::move(wrapper);
  }
  return decision;
}

bool DynamicModuleRouteSpecifier::recordsACacheableRoute(const RouteSpecifierContext& context,
                                                         const Decision& decision) {
  return decision.failure == Failure::None && !context.depends_on_request_path;
}

void DynamicModuleRouteSpecifier::verifyCachedRoute(const RouteSpecifierContext& context,
                                                    const Decision& decision,
                                                    const BuiltRouteLookup& lookup) const {
  config_->stats().built_route_cache_verify_.inc();
  // A recording the cache could not hold, such as one that depends on the request path, cannot
  // confirm the cached route even when it records the same route for this request.
  if (recordsACacheableRoute(context, decision) &&
      recordsTheCachedRoute(*lookup.hit, decision.recorded, context.user_data)) {
    return;
  }
  config_->stats().built_route_cache_verify_mismatch_.inc();
  lookup.cache->erase(lookup.key, context.selected_route.get());
  ENVOY_LOG_EVERY_POW_2(
      warn,
      "dynamic module route specifier evicted a cached route its verification did not confirm");
}

} // namespace DynamicModules
} // namespace RouteSpecifiers
} // namespace Extensions
} // namespace Envoy
