#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/router/route_specifiers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/hash_policy.h"
#include "envoy/router/route_specifier.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"
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

// The default custom stat namespace which prepends all user-defined metrics.
// This can be overridden via the ``metrics_namespace`` field in ``DynamicModuleConfig``.
constexpr absl::string_view DefaultMetricsNamespace = "dynamicmodulescustom";

// The statistics of a route specifier.
#define ALL_DYNAMIC_MODULE_ROUTE_SPECIFIER_STATS(COUNTER, HISTOGRAM)                               \
  COUNTER(decision_pass_through)                                                                   \
  COUNTER(decision_override)                                                                       \
  COUNTER(decision_select_template)                                                                \
  COUNTER(decision_no_route)                                                                       \
  COUNTER(decision_error)                                                                          \
  COUNTER(runtime_skipped)                                                                         \
  COUNTER(failure_module_error)                                                                    \
  COUNTER(failure_template_not_selected)                                                           \
  COUNTER(failure_template_match_failed)                                                           \
  COUNTER(failure_override_without_route)                                                          \
  COUNTER(failure_override_on_non_route_entry)                                                     \
  COUNTER(failure_route_metadata)                                                                  \
  HISTOGRAM(on_route_duration, Microseconds)                                                       \
  HISTOGRAM(specifier_duration, Microseconds)

struct RouteSpecifierStats {
  ALL_DYNAMIC_MODULE_ROUTE_SPECIFIER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_HISTOGRAM_STRUCT)
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
};

using RouteOverrideMap = absl::flat_hash_map<std::string, RouteOverride>;

// A header mutation a module recorded for a request.
struct HeaderMutation {
  Http::LowerCaseString key;
  std::string value;
  envoy_dynamic_module_type_route_specifier_header_append_action action;
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
  std::optional<std::string> path;
  std::optional<std::string> host;
  std::vector<HeaderMutation> request_headers_to_add;
  std::vector<Http::LowerCaseString> request_headers_to_remove;
  std::vector<HeaderMutation> response_headers_to_add;
  std::vector<Http::LowerCaseString> response_headers_to_remove;

  // Whether anything that only a route entry can carry was recorded.
  bool hasRouteEntryOverrides() const;
  // Whether anything that any route can carry was recorded.
  bool hasRouteOverrides() const;
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
  bool failClosed() const { return fail_closed_; }
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
  const bool fail_closed_;
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
  // The selected template evaluated against the request, set when set_template succeeds so that the
  // getters reflect the route being produced. Null keeps the getters on the route matching
  // resolved, whether no template was selected or its match did not hold.
  Envoy::Router::RouteConstSharedPtr selected_route;
  envoy_dynamic_module_type_route_specifier_chain_status chain_status{
      envoy_dynamic_module_type_route_specifier_chain_status_Default};
  RouteOverrides overrides;
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
  DynamicModuleRoute(Envoy::Router::RouteConstSharedPtr route,
                     DynamicModuleRouteSpecifierConfigSharedPtr config, RouteOverrides&& overrides);

  // Router::Route
  const envoy::config::core::v3::Metadata& metadata() const override;
  const Envoy::Config::TypedMetadata& typedMetadata() const override;
  std::optional<bool> filterDisabled(absl::string_view name) const override;

protected:
  const DynamicModuleRouteSpecifierConfigSharedPtr config_;
  const RouteOverrides overrides_;
  // Null when the module recorded no route metadata, so that both accessors fall back to the route.
  const Envoy::Config::MetadataPackPtr<Envoy::Router::HttpRouteTypedMetadataFactory> metadata_pack_;
};

/**
 * Route entry that delegates to the route a decision produced and applies the overrides a module
 * recorded for the request.
 */
class DynamicModuleRouteEntry : public Envoy::Router::DelegatingRouteEntry {
public:
  DynamicModuleRouteEntry(Envoy::Router::RouteConstSharedPtr route,
                          DynamicModuleRouteSpecifierConfigSharedPtr config,
                          RouteOverrides&& overrides);

  // Router::Route
  const envoy::config::core::v3::Metadata& metadata() const override;
  const Envoy::Config::TypedMetadata& typedMetadata() const override;
  std::optional<bool> filterDisabled(absl::string_view name) const override;

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
  const DynamicModuleRouteSpecifierConfigSharedPtr config_;
  const RouteOverrides overrides_;
  const Envoy::Config::MetadataPackPtr<Envoy::Router::HttpRouteTypedMetadataFactory> metadata_pack_;
};

// Why Envoy could not honor the decision of a module. There is one value per failure statistic of
// the route specifier.
enum class Failure {
  // The decision was honored.
  None,
  // The module returned the Error decision.
  ModuleError,
  // The decision was SelectTemplate without a successful set_template.
  TemplateNotSelected,
  // The match of the selected template does not hold for the request.
  TemplateMatchFailed,
  // The decision was Override while route matching resolved no route.
  OverrideWithoutRoute,
  // Route entry overrides were recorded for a route that answers the request directly.
  OverrideOnNonRouteEntry,
  // The recorded route metadata was rejected by a typed metadata factory.
  RouteMetadata,
};

/**
 * RouteSpecifier that delegates the route decision to a dynamic module.
 */
class DynamicModuleRouteSpecifier : public Envoy::Router::RouteSpecifier,
                                    public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  explicit DynamicModuleRouteSpecifier(DynamicModuleRouteSpecifierConfigSharedPtr config)
      : config_(std::move(config)) {}

  // Router::RouteSpecifier
  Envoy::Router::OnRouteResult onRoute(Envoy::Router::RouteConstSharedPtr route,
                                       const Http::RequestHeaderMap& headers,
                                       const StreamInfo::StreamInfo& stream_info,
                                       uint64_t random) const override;

private:
  // The route a decision produced, along with why it could not be produced.
  struct Decision {
    Envoy::Router::RouteConstSharedPtr route;
    Envoy::Router::OnRouteResultStatus status{Envoy::Router::OnRouteResultStatus::Continue};
    Failure failure{Failure::None};
  };

  // decision is the raw value the module returned, which may be outside the known enum values.
  Decision resolve(RouteSpecifierContext& context, uint32_t decision) const;
  // The route the module asked for, without the failure policy applied.
  Decision wrap(Envoy::Router::RouteConstSharedPtr route, RouteSpecifierContext& context,
                Envoy::Router::OnRouteResultStatus status) const;

  const DynamicModuleRouteSpecifierConfigSharedPtr config_;
};

} // namespace DynamicModules
} // namespace RouteSpecifiers
} // namespace Extensions
} // namespace Envoy
