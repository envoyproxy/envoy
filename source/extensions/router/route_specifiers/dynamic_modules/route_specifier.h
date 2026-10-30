#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/random_generator.h"
#include "envoy/common/regex.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/router/route_specifiers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/hash_policy.h"
#include "envoy/router/route_specifier.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"
#include "source/common/config/metadata.h"
#include "source/common/router/delegating_route_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/dynamic_modules/metric_registry.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace RouteSpecifiers {
namespace DynamicModules {

using DynamicModuleRouteSpecifierProto =
    envoy::extensions::router::route_specifiers::dynamic_modules::v3::DynamicModuleRouteSpecifier;

// Type aliases for function pointers resolved from the module.
using OnRouteSpecifierConfigNewType = decltype(&envoy_dynamic_module_on_route_specifier_config_new);
using OnRouteSpecifierConfigDestroyType =
    decltype(&envoy_dynamic_module_on_route_specifier_config_destroy);
using OnRouteSpecifierOnRouteType = decltype(&envoy_dynamic_module_on_route_specifier_on_route);
using OnRouteSpecifierRouteDestroyType =
    decltype(&envoy_dynamic_module_on_route_specifier_route_destroy);
using OnRouteSpecifierUserDataDiscardType =
    decltype(&envoy_dynamic_module_on_route_specifier_user_data_discard);

// The default custom stat namespace which prepends all user-defined metrics.
// This can be overridden via the ``metrics_namespace`` field in ``DynamicModuleConfig``.
constexpr absl::string_view DefaultMetricsNamespace = "dynamicmodulescustom";

// The statistics of a route specifier.
#define ALL_DYNAMIC_MODULE_ROUTE_SPECIFIER_STATS(COUNTER, GAUGE, HISTOGRAM)                        \
  COUNTER(decision_pass_through)                                                                   \
  COUNTER(decision_has_override)                                                                   \
  COUNTER(decision_has_template)                                                                   \
  COUNTER(decision_no_route)                                                                       \
  COUNTER(decision_error)                                                                          \
  COUNTER(decision_reuse_previous)                                                                 \
  COUNTER(route_skipped)                                                                           \
  COUNTER(unknown_status)                                                                          \
  COUNTER(runtime_skipped)                                                                         \
  COUNTER(failure_module_error)                                                                    \
  COUNTER(failure_template_not_selected)                                                           \
  COUNTER(failure_template_match_failed)                                                           \
  COUNTER(failure_override_without_route)                                                          \
  COUNTER(failure_override_on_non_route_entry)                                                     \
  COUNTER(failure_route_metadata)                                                                  \
  COUNTER(reuse_previous_rejected)                                                                 \
  COUNTER(route_destroy)                                                                           \
  COUNTER(user_data_discard)                                                                       \
  COUNTER(rewrite_not_applied)                                                                     \
  COUNTER(built_route_cache_disabled)                                                              \
  COUNTER(built_route_cache_hit)                                                                   \
  COUNTER(built_route_cache_miss)                                                                  \
  COUNTER(built_route_cache_verify)                                                                \
  COUNTER(built_route_cache_verify_mismatch)                                                       \
  COUNTER(built_route_cache_evict)                                                                 \
  GAUGE(built_route_cache_entries, NeverImport)                                                    \
  HISTOGRAM(on_route_duration, Microseconds)                                                       \
  HISTOGRAM(specifier_duration, Microseconds)

struct RouteSpecifierStats {
  ALL_DYNAMIC_MODULE_ROUTE_SPECIFIER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                                           GENERATE_HISTOGRAM_STRUCT)
};

/**
 * Route properties built from a single route_overrides entry. These properties are
 * built from extensions and are read by the router through references that must outlive the
 * request, so they are built once at configuration time and selected by override_id on the request
 * path.
 */
struct RouteOverride {
  Envoy::Router::RetryPolicyConstSharedPtr retry_policy;
  Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_criteria;
  std::vector<Envoy::Router::ShadowPolicyPtr> shadow_policies;
  std::unique_ptr<Http::HashPolicy> hash_policy;
  std::unique_ptr<Envoy::Router::HedgePolicy> hedge_policy;
  std::unique_ptr<const Envoy::Router::RateLimitPolicy> rate_limit_policy;
  std::unique_ptr<const Envoy::Router::CorsPolicy> cors_policy;
  // Regex rewrite of the request path, compiled once at configuration load. Null when unset.
  Regex::CompiledMatcherPtr regex_rewrite;
  std::string regex_rewrite_substitution;
  // Route level properties, valid on any route rather than only a route entry.
  std::shared_ptr<const Envoy::Router::RouteTracing> tracing;
  envoy::config::core::v3::Metadata metadata;

  // Whether the override carries a property only a route entry can carry.
  bool hasRouteEntryProperties() const {
    return retry_policy != nullptr || metadata_match_criteria != nullptr ||
           !shadow_policies.empty() || hash_policy != nullptr || hedge_policy != nullptr ||
           rate_limit_policy != nullptr || cors_policy != nullptr || regex_rewrite != nullptr;
  }
};

using RouteOverrideMap = absl::flat_hash_map<std::string, RouteOverride>;

// A prefix rewrite a module recorded as a rule, applied to the path the request carries when the
// request headers are finalized.
struct PrefixRewriteRule {
  // The prefix the rule replaces. Unset when the rule replaces the whole path.
  std::optional<std::string> matched;
  std::string replacement;

  // The path the rule produces for a request path, or nullopt when the rule does not apply to it or
  // the result would exceed max_bytes.
  std::optional<std::string> apply(absl::string_view path, size_t max_bytes) const;
  // Whether apply produces a path, without building it.
  bool appliesTo(absl::string_view path, size_t max_bytes) const;

  bool operator==(const PrefixRewriteRule& other) const = default;
};

// A header mutation a module recorded for a request.
struct HeaderMutation {
  Http::LowerCaseString key;
  std::string value;
  envoy_dynamic_module_type_route_specifier_header_append_action action;

  bool operator==(const HeaderMutation& other) const = default;
};

/**
 * The overrides a module records for a request. They are applied to the route the decision produces
 * by the delegating routes below, and are only read on the worker thread that owns the request.
 */
struct RouteOverrides {
  std::string cluster_name;
  std::optional<std::chrono::milliseconds> timeout;
  std::optional<std::chrono::milliseconds> idle_timeout;
  std::optional<std::chrono::milliseconds> max_stream_duration;
  std::optional<uint64_t> request_body_buffer_limit;
  std::optional<Upstream::ResourcePriority> priority;
  std::optional<Http::Code> cluster_not_found_response_code;
  // Points into the override map of the configuration, which is immutable after construction. Null
  // when the module selected none.
  const RouteOverride* route_override{nullptr};
  // Metadata the module layered onto the route, keyed by namespace. Empty when the module set none.
  envoy::config::core::v3::Metadata route_metadata;
  absl::flat_hash_map<std::string, bool> filter_disabled;
  // Name the module recorded for the route it produces. Unset when the module set none.
  std::optional<std::string> route_name;
  std::optional<std::string> path;
  std::optional<PrefixRewriteRule> prefix_rewrite_rule;
  // Whether the regex rewrite of the selected route override applies when the request headers are
  // finalized rather than when the decision resolves.
  bool finalize_regex_rewrite{false};
  std::optional<std::string> host;
  std::vector<HeaderMutation> request_headers_to_add;
  std::vector<Http::LowerCaseString> request_headers_to_remove;
  std::vector<HeaderMutation> response_headers_to_add;
  std::vector<Http::LowerCaseString> response_headers_to_remove;

  // Whether anything that only a route entry can carry was recorded.
  bool hasRouteEntryOverrides() const;
  // Whether anything that any route can carry was recorded.
  bool hasRouteOverrides() const;

  // Whether two recordings build the same route. Every field must take part.
  bool operator==(const RouteOverrides& other) const;
};

// The runtime fraction the module is invoked for.
struct RuntimeFraction {
  std::string key;
  envoy::type::v3::FractionalPercent default_value;
};

/**
 * Configuration for a dynamic module route specifier. This resolves and holds the symbols used to
 * resolve routes along with the in-module configuration, the route templates and the route
 * overrides the module may select. It is shared by every route the specifier produces so that the
 * module and its configuration outlive all in-flight requests.
 *
 * Note: Symbol resolution and in-module config creation are done in the factory function
 * newDynamicModuleRouteSpecifierConfig() to provide graceful error handling. The constructor only
 * initializes basic members.
 */
class DynamicModuleRouteSpecifierConfig {
public:
  DynamicModuleRouteSpecifierConfig(const DynamicModuleRouteSpecifierProto& proto_config,
                                    absl::string_view specifier_config,
                                    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                    Envoy::Router::RouteSpecifierFactoryContext& context,
                                    absl::string_view metrics_namespace);

  ~DynamicModuleRouteSpecifierConfig();

  // A route template, along with its identifier and the kind of route it produces, which is fixed
  // by its action.
  struct Template {
    std::string id;
    Envoy::Router::MatchableRouteConstSharedPtr route;
    envoy_dynamic_module_type_route_specifier_route_kind kind;
    // Whether every match returns the same route, which the built route cache requires. A route
    // whose cluster is resolved per request builds a route for each request instead.
    bool cacheable;
  };

  /**
   * @param id the identifier of the route template.
   * @return the matching template, or nullptr when there is none. The returned pointer is valid for
   * the lifetime of this configuration.
   */
  const Template* routeTemplate(absl::string_view id) const;

  /**
   * @param override_id the identifier of the entry in route_overrides.
   * @return the matching override, or nullptr when there is none. The returned pointer is valid for
   * the lifetime of this configuration.
   */
  const RouteOverride* routeOverride(absl::string_view override_id) const;

  /**
   * Registers a route template from a serialized Route, building it with the route builder of the
   * configuration. Only valid while the in-module configuration is being created.
   * @param id the identifier the module selects the template with.
   * @param serialized_route the serialized envoy.config.route.v3.Route.
   * @return false when called outside configuration creation, when there is no route builder
   * because the specifier is configured on a route configuration, when the identifier is empty or
   * already used, when the bytes do not parse, or when the route is invalid.
   */
  bool registerRouteTemplate(absl::string_view id, absl::string_view serialized_route);

  const std::deque<std::string>& templateIds() const { return template_ids_; }
  const std::optional<RuntimeFraction>& runtimeFraction() const { return runtime_fraction_; }
  const std::string& specifierInstanceId() const { return specifier_instance_id_; }
  OnRouteSpecifierRouteDestroyType onRouteDestroy() const { return on_route_destroy_; }
  OnRouteSpecifierUserDataDiscardType onUserDataDiscard() const { return on_user_data_discard_; }
  uint32_t maxRewrittenPathBytes() const { return max_rewritten_path_bytes_; }
  bool hasBuiltRouteCache() const { return has_built_route_cache_; }
  bool failClosed() const { return fail_closed_; }
  bool continueMatchingOnFailure() const { return continue_matching_on_failure_; }
  Upstream::ClusterManager& clusterManager() const { return cluster_manager_; }
  Runtime::Loader& runtime() const { return runtime_; }
  TimeSource& timeSource() const { return time_source_; }
  RouteSpecifierStats& stats() const { return stats_; }

  // The corresponding in-module route specifier configuration.
  envoy_dynamic_module_type_route_specifier_config_module_ptr in_module_config_{nullptr};

  // The function pointers resolved from the module, guaranteed non-nullptr after
  // newDynamicModuleRouteSpecifierConfig() succeeds.
  OnRouteSpecifierConfigDestroyType on_config_destroy_{nullptr};
  OnRouteSpecifierOnRouteType on_route_{nullptr};
  // Optional hooks, null when the module does not export them, in which case Envoy skips them.
  OnRouteSpecifierRouteDestroyType on_route_destroy_{nullptr};
  OnRouteSpecifierUserDataDiscardType on_user_data_discard_{nullptr};

  // ----------------------------- Metrics Support -----------------------------
  // The shared registry holding all module-defined metrics.
  Extensions::DynamicModules::MetricRegistry& metrics() { return metrics_; }

  // Owns the scope the registry references. Must precede metrics_ so it initializes first.
  const Stats::ScopeSharedPtr metrics_scope_;
  // Shared metrics registry composed from metrics_scope_.
  Extensions::DynamicModules::MetricRegistry metrics_;
  // We only allow the module to create stats during on_route_specifier_config_new, and not later
  // from worker threads, so that we don't have to wrap the metrics registry pool in a lock.
  std::atomic<bool> stat_creation_frozen_{false};

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleRouteSpecifierConfig>>
  newDynamicModuleRouteSpecifierConfig(const DynamicModuleRouteSpecifierProto& proto_config,
                                       Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                       Envoy::Router::RouteSpecifierFactoryContext& context);

  // Declared first so that it is destroyed last, since everything below may run module code.
  const Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
  const std::string specifier_name_;
  const std::string specifier_config_;
  const std::string specifier_instance_id_;
  // Immutable after configuration load, so that the pointers routeTemplate() and
  // routeOverride() hand out stay valid. A module may add templates during config creation
  // with registerRouteTemplate(). template_ids_ is a deque so that a template id buffer handed to
  // the module keeps its address when a later registration grows the container.
  absl::flat_hash_map<std::string, Template> templates_;
  std::deque<std::string> template_ids_;
  RouteOverrideMap route_overrides_;
  // Valid only while the in-module configuration is being created, so that the module can register
  // route templates Envoy builds with the route builder of the configuration.
  Envoy::Router::RouteBuilder* config_new_route_builder_{nullptr};
  bool config_new_validate_clusters_{false};
  const std::optional<RuntimeFraction> runtime_fraction_;
  const uint32_t max_rewritten_path_bytes_;
  const bool has_built_route_cache_;
  const bool fail_closed_;
  const bool continue_matching_on_failure_;
  Upstream::ClusterManager& cluster_manager_;
  Runtime::Loader& runtime_;
  TimeSource& time_source_;
  const Stats::ScopeSharedPtr stats_scope_;
  mutable RouteSpecifierStats stats_;
};

using DynamicModuleRouteSpecifierConfigSharedPtr =
    std::shared_ptr<DynamicModuleRouteSpecifierConfig>;

/**
 * Creates a new DynamicModuleRouteSpecifierConfig for the given configuration.
 * @param proto_config the route specifier configuration.
 * @param dynamic_module the dynamic module to use.
 * @param context the factory context used to build the route templates.
 * @return a shared pointer to the new config object or an error if symbol resolution, route
 * template construction, route override construction or in-module initialization failed.
 */
absl::StatusOr<DynamicModuleRouteSpecifierConfigSharedPtr>
newDynamicModuleRouteSpecifierConfig(const DynamicModuleRouteSpecifierProto& proto_config,
                                     Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                     Envoy::Router::RouteSpecifierFactoryContext& context);

// The settings of the built route cache.
struct BuiltRouteCacheSettings {
  uint32_t max_entries;
  std::chrono::milliseconds sweep_interval;
  // Zero disables verification.
  uint32_t verify_one_in;
};

// A route the built route cache holds.
struct CachedRoute {
  Envoy::Router::RouteConstSharedPtr route;
  // The overrides the route was built from, pointing into it. Null when it is the template itself.
  const RouteOverrides* recorded{nullptr};
  std::optional<uint64_t> user_data;
};

/**
 * A per worker cache of the routes a specifier builds from route templates, keyed by a key the
 * module sets and by the template. It lives in a thread local slot the specifier owns, because
 * every cached route holds the configuration. Only its own worker uses it, so it takes no lock.
 */
class BuiltRouteCache : public ThreadLocal::ThreadLocalObject {
public:
  BuiltRouteCache(Event::Dispatcher& dispatcher, DynamicModuleRouteSpecifierConfigSharedPtr config,
                  const BuiltRouteCacheSettings& settings, Random::RandomGenerator& random);
  ~BuiltRouteCache() override;

  // The route cached for the key and the template that serves a request for the path, or nullptr.
  // A hit marks the route used.
  const CachedRoute* lookup(uint64_t key, const Envoy::Router::Route* base, absl::string_view path);
  // Caches a route unless one is cached for the key and the template, evicting one that was not
  // used recently when the cache is full.
  void insert(uint64_t key, const Envoy::Router::Route* base, CachedRoute route);
  void erase(uint64_t key, const Envoy::Router::Route* base);
  // Whether a hit is verified, with a chance of one in verify_one_in.
  bool sampleVerification();

private:
  // The key the module set and the template route, which the cached route keeps alive so that its
  // address is not reused.
  using Key = std::pair<uint64_t, const Envoy::Router::Route*>;
  struct Entry {
    Key key;
    CachedRoute value;
    // The sweep tick of the last use and the reference bit of the CLOCK eviction.
    uint64_t last_used_tick;
    bool referenced;
  };

  // The index of the entry the CLOCK hand evicts next. Only called on a full cache, which keeps the
  // hand below max_entries.
  size_t clockVictim();
  // Evicts the entries idle for longer than the entry age.
  void sweep();
  void removeAt(size_t index);

  const DynamicModuleRouteSpecifierConfigSharedPtr config_;
  const BuiltRouteCacheSettings settings_;
  Random::RandomGenerator& random_;
  std::vector<Entry> entries_;
  absl::flat_hash_map<Key, size_t> index_;
  size_t hand_{0};
  uint64_t tick_{0};
  const Event::TimerPtr sweep_timer_;
};

// What lookup_built_route found for the selected template.
struct BuiltRouteLookup {
  BuiltRouteCache* cache;
  uint64_t key;
  // Set on a hit.
  std::optional<CachedRoute> hit;
  bool verify{false};
};

/**
 * Per-decision context passed to the module as the route_specifier_context_envoy_ptr. It bundles
 * the request state the module reads and the decision it is building. Valid only for the duration
 * of a single envoy_dynamic_module_on_route_specifier_on_route call.
 */
struct RouteSpecifierContext {
  const DynamicModuleRouteSpecifierConfig& config;
  const Envoy::Router::RouteConstSharedPtr& input_route;
  const Http::RequestHeaderMap& headers;
  const StreamInfo::StreamInfo& stream_info;
  const uint64_t random_value;
  const DynamicModuleRouteSpecifierConfig::Template* selected_template{nullptr};
  // Whether a set_route_template call named an identifier that is not declared. When no template
  // ends up selected and the decision is left Unspecified, the failure policy applies rather than
  // silently falling back to the route the specifier was given. A module that wants to probe for
  // a template without committing checks the configuration getters instead.
  bool template_selection_failed{false};
  // The selected template evaluated against the request, set when set_route_template succeeds so
  // that the getters reflect the route being produced. Null keeps the getters on the route matching
  // resolved, whether no template was selected or its match did not hold.
  Envoy::Router::RouteConstSharedPtr selected_route;
  // The decision set_decision recorded, which may hold a value outside the named enumerators,
  // for example from a newer ABI. Unspecified when the module recorded none.
  envoy_dynamic_module_type_route_specifier_decision decision{
      envoy_dynamic_module_type_route_specifier_decision_Unspecified};
  RouteOverrides overrides;
  // A u64 the module recorded on the route it produces. Forces the produced route to be wrapped
  // even with no override, so the route destroy hook fires for it. Unset when the module set none.
  std::optional<uint64_t> user_data;
  // Whether the recording depends on the path of the current request, through set_prefix_rewrite,
  // a rule rejected for that path or a rule replaced after it was accepted, which keeps the route
  // out of the built route cache.
  bool depends_on_request_path{false};
  // The slot of the built route cache. Null when the specifier configures none.
  ThreadLocal::TypedSlot<BuiltRouteCache>* built_route_cache_slot{nullptr};
  // What the last lookup_built_route found for the selected template. Unset when no lookup is in
  // effect for the decision.
  std::optional<BuiltRouteLookup> built_route_lookup;
  // Redirect locations built for the getters, kept in a stable container so a buffer handed to the
  // module stays valid until the hook returns even after the current route changes. Built once per
  // route so repeated reads for the same route return the same buffer.
  std::deque<std::string> input_redirect_locations;
  const Envoy::Router::Route* input_redirect_route{nullptr};

  // The route the getters read. It is the selected template evaluated against the request when one
  // was selected, otherwise the route matching resolved.
  const Envoy::Router::RouteConstSharedPtr& currentRoute() const {
    return selected_route != nullptr ? selected_route : input_route;
  }
};

/**
 * Route that delegates to the route a decision produced and applies the overrides a module recorded
 * for the request. It holds the configuration so that the module outlives the request.
 */
class DynamicModuleRoute : public Envoy::Router::DelegatingRoute {
public:
  DynamicModuleRoute(
      Envoy::Router::RouteConstSharedPtr route, DynamicModuleRouteSpecifierConfigSharedPtr config,
      RouteOverrides&& overrides,
      Envoy::Config::MetadataPackPtr<Envoy::Router::HttpRouteTypedMetadataFactory> metadata_pack,
      std::optional<uint64_t> user_data);
  ~DynamicModuleRoute() override;

  const RouteOverrides& recordedOverrides() const { return overrides_; }

  // Router::Route
  const std::string& routeName() const override;
  const envoy::config::core::v3::Metadata& metadata() const override;
  const Envoy::Config::TypedMetadata& typedMetadata() const override;
  std::optional<bool> filterDisabled(absl::string_view name) const override;
  const Envoy::Router::RouteTracing* tracingConfig() const override;

protected:
  const DynamicModuleRouteSpecifierConfigSharedPtr config_;
  const RouteOverrides overrides_;
  // Null when the module recorded no route metadata, so that both accessors fall back to the route.
  const Envoy::Config::MetadataPackPtr<Envoy::Router::HttpRouteTypedMetadataFactory> metadata_pack_;
  // Set when the module recorded user data, passed to the route destroy hook when this is
  // destroyed.
  const std::optional<uint64_t> user_data_;
};

/**
 * Route entry that delegates to the route a decision produced and applies the overrides a module
 * recorded for the request.
 */
class DynamicModuleRouteEntry : public Envoy::Router::DelegatingRouteEntry {
public:
  DynamicModuleRouteEntry(
      Envoy::Router::RouteConstSharedPtr route, DynamicModuleRouteSpecifierConfigSharedPtr config,
      RouteOverrides&& overrides,
      Envoy::Config::MetadataPackPtr<Envoy::Router::HttpRouteTypedMetadataFactory> metadata_pack,
      std::optional<uint64_t> user_data);
  ~DynamicModuleRouteEntry() override;

  const RouteOverrides& recordedOverrides() const { return overrides_; }

  // Router::Route
  const std::string& routeName() const override;
  const envoy::config::core::v3::Metadata& metadata() const override;
  const Envoy::Config::TypedMetadata& typedMetadata() const override;
  std::optional<bool> filterDisabled(absl::string_view name) const override;
  const Envoy::Router::RouteTracing* tracingConfig() const override;

  // Router::RouteEntry
  const std::string& clusterName() const override;
  std::chrono::milliseconds timeout() const override;
  std::optional<std::chrono::milliseconds> idleTimeout() const override;
  std::optional<std::chrono::milliseconds> maxStreamDuration() const override;
  bool usingNewTimeouts() const override;
  uint64_t requestBodyBufferLimit() const override;
  Upstream::ResourcePriority priority() const override;
  Http::Code clusterNotFoundResponseCode() const override;
  const Envoy::Router::RetryPolicyConstSharedPtr& retryPolicy() const override;
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const override;
  const std::vector<Envoy::Router::ShadowPolicyPtr>& shadowPolicies() const override;
  const Http::HashPolicy* hashPolicy() const override;
  const Envoy::Router::HedgePolicy& hedgePolicy() const override;
  const Envoy::Router::RateLimitPolicy& rateLimitPolicy() const override;
  const Envoy::Router::CorsPolicy* corsPolicy() const override;
  std::string currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers,
                                         const Formatter::Context& context,
                                         const StreamInfo::StreamInfo& stream_info) const override;
  void finalizeRequestHeaders(Http::RequestHeaderMap& headers, const Formatter::Context& context,
                              const StreamInfo::StreamInfo& stream_info,
                              bool insert_envoy_original_path) const override;
  Http::HeaderTransforms requestHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                 bool do_formatting = true) const override;
  void finalizeResponseHeaders(Http::ResponseHeaderMap& headers, const Formatter::Context& context,
                               const StreamInfo::StreamInfo& stream_info) const override;
  Http::HeaderTransforms responseHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                  bool do_formatting = true) const override;

private:
  // Whether a prefix rewrite rule or a route override regex rewrite applies when the request
  // headers are finalized.
  bool hasFinalizationRewrite() const;
  // The path the first of those rewrites that applies produces, or nullopt when none applies.
  std::optional<std::string> finalizationRewrite(absl::string_view path) const;

  const DynamicModuleRouteSpecifierConfigSharedPtr config_;
  const RouteOverrides overrides_;
  const Envoy::Config::MetadataPackPtr<Envoy::Router::HttpRouteTypedMetadataFactory> metadata_pack_;
  const std::optional<uint64_t> user_data_;
};

// Why Envoy could not honor the decision of a module. There is one value per failure statistic of
// the route specifier.
enum class Failure {
  // The decision was honored.
  None,
  // The module recorded the Error decision, recorded a decision this build does not know, or
  // returned a status this build does not know.
  ModuleError,
  // A template selection named an identifier that is not declared, no template ended up
  // selected, and the decision was left Unspecified.
  TemplateNotSelected,
  // The match of the selected template does not hold for the request.
  TemplateMatchFailed,
  // An override or user data was recorded, with no template to build a route from, while route
  // matching resolved no route to apply them to.
  OverrideWithoutRoute,
  // Route entry overrides were recorded for a route that answers the request directly.
  OverrideOnNonRouteEntry,
  // The recorded route metadata was rejected by a typed metadata factory.
  RouteMetadata,
  // ReusePrevious was returned but the stream has no previous route.
  ReusePreviousRejected,
};

/**
 * RouteSpecifier that delegates the route decision to a dynamic module.
 */
class DynamicModuleRouteSpecifier : public Envoy::Router::RouteSpecifier,
                                    public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  // Must be called on the main thread when the configuration sets built_route_cache, since it
  // allocates the thread local slot of the cache.
  DynamicModuleRouteSpecifier(DynamicModuleRouteSpecifierConfigSharedPtr config,
                              const DynamicModuleRouteSpecifierProto& proto_config,
                              ThreadLocal::SlotAllocator& tls, Random::RandomGenerator& random);

  // Router::RouteSpecifier
  Envoy::Router::OnRouteResult
  onRoute(Envoy::Router::RouteConstSharedPtr route, const Http::RequestHeaderMap& headers,
          const StreamInfo::StreamInfo& stream_info, uint64_t random,
          Envoy::Router::OnRouteInputStatus input_status) const override;

private:
  // The outcome of a decision, holding the route it produced, the chain status and the reason it
  // could not be honored. A chain status of StopIterationAndSkipRoute asks route matching to
  // carry on with the next route.
  struct Decision {
    Envoy::Router::RouteConstSharedPtr route;
    Envoy::Router::OnRouteStatus status{Envoy::Router::OnRouteStatus::Continue};
    Failure failure{Failure::None};
    // The overrides the route was built from, pointing into it. Null when wrap built no wrapper.
    const RouteOverrides* recorded{nullptr};
  };

  // on_route_status is the raw value the module returned, which may be outside the known enum
  // values. The decision the module recorded is read from the context.
  Decision resolve(RouteSpecifierContext& context, uint32_t on_route_status) const;
  // The route the module asked for, without the failure policy applied.
  Decision wrap(Envoy::Router::RouteConstSharedPtr route, RouteSpecifierContext& context,
                Envoy::Router::OnRouteStatus status) const;
  // Whether the decision recorded a route the cache can hold, which serves every later request that
  // looks the key up.
  static bool recordsACacheableRoute(const RouteSpecifierContext& context,
                                     const Decision& decision);
  // Evicts the cached route of a verified hit unless the decision recorded it again, as a route the
  // cache can hold.
  void verifyCachedRoute(const RouteSpecifierContext& context, const Decision& decision,
                         const BuiltRouteLookup& lookup) const;

  const DynamicModuleRouteSpecifierConfigSharedPtr config_;
  // Null when the configuration sets no built_route_cache.
  ThreadLocal::TypedSlotPtr<BuiltRouteCache> built_route_cache_;
};

} // namespace DynamicModules
} // namespace RouteSpecifiers
} // namespace Extensions
} // namespace Envoy
