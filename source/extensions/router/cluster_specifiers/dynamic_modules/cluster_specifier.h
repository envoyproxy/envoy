#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/extensions/router/cluster_specifiers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/hash_policy.h"
#include "envoy/router/cluster_specifier_plugin.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"
#include "source/common/router/delegating_route_impl.h"
#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace DynamicModules {

using DynamicModuleClusterSpecifierProto = envoy::extensions::router::cluster_specifiers::
    dynamic_modules::v3::DynamicModuleClusterSpecifier;

// Type aliases for function pointers resolved from the module.
using OnClusterSpecifierConfigNewType =
    decltype(&envoy_dynamic_module_on_cluster_specifier_config_new);
using OnClusterSpecifierConfigDestroyType =
    decltype(&envoy_dynamic_module_on_cluster_specifier_config_destroy);
using OnClusterSpecifierSelectType = decltype(&envoy_dynamic_module_on_cluster_specifier_select);

/**
 * Route action properties built from a single route_action_overrides entry. These properties are
 * built from extensions and are read by the router through references that must outlive the
 * request, so they are built once at configuration time and selected by name on the request path.
 */
struct RouteActionOverride {
  Envoy::Router::RetryPolicyConstSharedPtr retry_policy;
  Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_criteria;
  std::vector<Envoy::Router::ShadowPolicyPtr> shadow_policies;
  std::unique_ptr<Http::HashPolicy> hash_policy;
};

using RouteActionOverrideMap = absl::flat_hash_map<std::string, RouteActionOverride>;

// The default custom stat namespace which prepends all user-defined metrics.
// This can be overridden via the ``metrics_namespace`` field in ``DynamicModuleConfig``.
constexpr absl::string_view DefaultMetricsNamespace = "dynamicmodulescustom";

/**
 * Configuration for a dynamic module cluster specifier. This resolves and holds the symbols used
 * for cluster selection along with the in-module configuration and the route action overrides the
 * module may select. It is shared by every route entry the plugin creates so that the module and
 * its configuration outlive all in-flight requests.
 *
 * Note: Symbol resolution and in-module config creation are done in the factory function
 * newDynamicModuleClusterSpecifierConfig() to provide graceful error handling. The constructor only
 * initializes basic members.
 */
class DynamicModuleClusterSpecifierConfig {
public:
  DynamicModuleClusterSpecifierConfig(absl::string_view specifier_name,
                                      absl::string_view specifier_config,
                                      Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                      RouteActionOverrideMap route_action_overrides,
                                      Upstream::ClusterManager& cluster_manager,
                                      Stats::Scope& stats_scope,
                                      absl::string_view metrics_namespace);

  ~DynamicModuleClusterSpecifierConfig();

  /**
   * @param name the key of the entry in the route_action_overrides map.
   * @return the matching override, or nullptr when there is none. The returned pointer is valid for
   * the lifetime of this configuration.
   */
  const RouteActionOverride* routeActionOverride(absl::string_view name) const;

  /**
   * Validates the statically named mirror clusters of every route action override. The route level
   * check only walks the mirror policies of the matched route, which never include these.
   * @param cluster_manager the cluster manager to look the clusters up in.
   * @return an error naming the first override that mirrors to an unknown cluster.
   */
  absl::Status validateClusters(const Upstream::ClusterManager& cluster_manager) const;

  /**
   * @return the cluster manager used to look up clusters during selection.
   */
  Upstream::ClusterManager& clusterManager() const { return cluster_manager_; }

  // The corresponding in-module cluster specifier configuration.
  envoy_dynamic_module_type_cluster_specifier_config_module_ptr in_module_config_{nullptr};

  // The function pointers resolved from the module. All are guaranteed non-nullptr after
  // newDynamicModuleClusterSpecifierConfig() succeeds.
  OnClusterSpecifierConfigDestroyType on_config_destroy_{nullptr};
  OnClusterSpecifierSelectType on_select_{nullptr};

  // ----------------------------- Metrics Support -----------------------------

  class ModuleCounterHandle {
  public:
    ModuleCounterHandle(Stats::Counter& counter) : counter_(counter) {}
    void add(uint64_t value) const { counter_.add(value); }

  private:
    Stats::Counter& counter_;
  };

  class ModuleCounterVecHandle {
  public:
    ModuleCounterVecHandle(Stats::StatName name, Stats::StatNameVec label_names)
        : name_(name), label_names_(label_names) {}
    const Stats::StatNameVec& getLabelNames() const { return label_names_; }
    void add(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::counterFromElements(scope, {name_}, tags).add(amount);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
  };

  class ModuleGaugeHandle {
  public:
    ModuleGaugeHandle(Stats::Gauge& gauge) : gauge_(gauge) {}
    void add(uint64_t value) const { gauge_.add(value); }
    void sub(uint64_t value) const { gauge_.sub(value); }
    void set(uint64_t value) const { gauge_.set(value); }

  private:
    Stats::Gauge& gauge_;
  };

  class ModuleGaugeVecHandle {
  public:
    ModuleGaugeVecHandle(Stats::StatName name, Stats::StatNameVec label_names,
                         Stats::Gauge::ImportMode import_mode)
        : name_(name), label_names_(label_names), import_mode_(import_mode) {}
    const Stats::StatNameVec& getLabelNames() const { return label_names_; }
    void add(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).add(amount);
    }
    void sub(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).sub(amount);
    }
    void set(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).set(amount);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
    Stats::Gauge::ImportMode import_mode_;
  };

  class ModuleHistogramHandle {
  public:
    ModuleHistogramHandle(Stats::Histogram& histogram) : histogram_(histogram) {}
    void recordValue(uint64_t value) const { histogram_.recordValue(value); }

  private:
    Stats::Histogram& histogram_;
  };

  class ModuleHistogramVecHandle {
  public:
    ModuleHistogramVecHandle(Stats::StatName name, Stats::StatNameVec label_names,
                             Stats::Histogram::Unit unit)
        : name_(name), label_names_(label_names), unit_(unit) {}
    const Stats::StatNameVec& getLabelNames() const { return label_names_; }
    void recordValue(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                     uint64_t value) const {
      ASSERT(tags.has_value());
      Stats::Utility::histogramFromElements(scope, {name_}, unit_, tags).recordValue(value);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
    Stats::Histogram::Unit unit_;
  };

#define ID_TO_INDEX(id) ((id) - 1)

  size_t addCounter(ModuleCounterHandle&& counter) {
    counters_.push_back(std::move(counter));
    return counters_.size();
  }
  size_t addCounterVec(ModuleCounterVecHandle&& counter) {
    counter_vecs_.push_back(std::move(counter));
    return counter_vecs_.size();
  }
  size_t addGauge(ModuleGaugeHandle&& gauge) {
    gauges_.push_back(std::move(gauge));
    return gauges_.size();
  }
  size_t addGaugeVec(ModuleGaugeVecHandle&& gauge) {
    gauge_vecs_.push_back(std::move(gauge));
    return gauge_vecs_.size();
  }
  size_t addHistogram(ModuleHistogramHandle&& histogram) {
    histograms_.push_back(std::move(histogram));
    return histograms_.size();
  }
  size_t addHistogramVec(ModuleHistogramVecHandle&& histogram) {
    histogram_vecs_.push_back(std::move(histogram));
    return histogram_vecs_.size();
  }

  OptRef<const ModuleCounterHandle> getCounterById(size_t id) const {
    if (id == 0 || id > counters_.size()) {
      return {};
    }
    return counters_[ID_TO_INDEX(id)];
  }
  OptRef<const ModuleCounterVecHandle> getCounterVecById(size_t id) const {
    if (id == 0 || id > counter_vecs_.size()) {
      return {};
    }
    return counter_vecs_[ID_TO_INDEX(id)];
  }
  OptRef<const ModuleGaugeHandle> getGaugeById(size_t id) const {
    if (id == 0 || id > gauges_.size()) {
      return {};
    }
    return gauges_[ID_TO_INDEX(id)];
  }
  OptRef<const ModuleGaugeVecHandle> getGaugeVecById(size_t id) const {
    if (id == 0 || id > gauge_vecs_.size()) {
      return {};
    }
    return gauge_vecs_[ID_TO_INDEX(id)];
  }
  OptRef<const ModuleHistogramHandle> getHistogramById(size_t id) const {
    if (id == 0 || id > histograms_.size()) {
      return {};
    }
    return histograms_[ID_TO_INDEX(id)];
  }
  OptRef<const ModuleHistogramVecHandle> getHistogramVecById(size_t id) const {
    if (id == 0 || id > histogram_vecs_.size()) {
      return {};
    }
    return histogram_vecs_[ID_TO_INDEX(id)];
  }

#undef ID_TO_INDEX

  const Stats::ScopeSharedPtr stats_scope_;
  Stats::StatNamePool stat_name_pool_;
  // We only allow the module to create stats during on_cluster_specifier_config_new, and not later
  // from worker threads, so that we don't have to wrap stat_name_pool_ in a lock. Per-request label
  // values use a stack-local Stats::StatNameDynamicPool in the record callbacks (see abi_impl.cc).
  std::atomic<bool> stat_creation_frozen_{false};

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleClusterSpecifierConfig>>
  newDynamicModuleClusterSpecifierConfig(
      const DynamicModuleClusterSpecifierProto& proto_config,
      Extensions::DynamicModules::DynamicModulePtr dynamic_module,
      Server::Configuration::ServerFactoryContext& context);

  const std::string specifier_name_;
  const std::string specifier_config_;
  const Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
  Upstream::ClusterManager& cluster_manager_;
  // Const after construction so that the pointers routeActionOverride() hands out stay valid.
  const RouteActionOverrideMap route_action_overrides_;

  std::vector<ModuleCounterHandle> counters_;
  std::vector<ModuleCounterVecHandle> counter_vecs_;
  std::vector<ModuleGaugeHandle> gauges_;
  std::vector<ModuleGaugeVecHandle> gauge_vecs_;
  std::vector<ModuleHistogramHandle> histograms_;
  std::vector<ModuleHistogramVecHandle> histogram_vecs_;
};

using DynamicModuleClusterSpecifierConfigSharedPtr =
    std::shared_ptr<DynamicModuleClusterSpecifierConfig>;

/**
 * Creates a new DynamicModuleClusterSpecifierConfig for the given configuration.
 * @param proto_config the cluster specifier configuration.
 * @param dynamic_module the dynamic module to use.
 * @param context the server factory context used to build the route action overrides.
 * @return a shared pointer to the new config object or an error if symbol resolution, route action
 * override construction or in-module initialization failed.
 */
absl::StatusOr<DynamicModuleClusterSpecifierConfigSharedPtr>
newDynamicModuleClusterSpecifierConfig(const DynamicModuleClusterSpecifierProto& proto_config,
                                       Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                       Server::Configuration::ServerFactoryContext& context);

/**
 * The route properties a module selected for a request. The selection callbacks fill this in and it
 * is applied to the route entry only once the module reports a decision, so a module that gives up
 * part way through leaves the matched route untouched.
 */
struct ClusterSpecifierSelection {
  // Empty when the module selected no cluster. This is a string rather than an optional string so
  // that a refresh never destroys it, keeping the reference clusterName() returns valid. A refresh
  // does replace the contents, so a caller that needs the value past the next refresh must copy it.
  std::string cluster_name;
  std::optional<std::chrono::milliseconds> timeout;
  std::optional<std::chrono::milliseconds> idle_timeout;
  std::optional<uint64_t> request_body_buffer_limit;
  std::optional<Upstream::ResourcePriority> priority;
  std::optional<Http::Code> cluster_not_found_response_code;
  // Points into the override map of the cluster specifier configuration, which is immutable after
  // construction.
  const RouteActionOverride* route_action_override{nullptr};
};

/**
 * Route entry that delegates to the matched route and applies the properties the dynamic module
 * selected for the request. A route entry is created per request and used by a single worker
 * thread, so the selection is refreshed in place. Each refresh that reaches a decision replaces the
 * previous one, so the module states the whole selection on every call.
 */
class DynamicModuleRouteEntry : public Envoy::Router::DelegatingRouteEntry,
                                public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  DynamicModuleRouteEntry(Envoy::Router::RouteEntryAndRouteConstSharedPtr parent,
                          DynamicModuleClusterSpecifierConfigSharedPtr config,
                          uint64_t random_value)
      : DelegatingRouteEntry(std::move(parent)), config_(std::move(config)),
        random_value_(random_value) {}

  // Router::RouteEntry
  const std::string& clusterName() const override {
    return selection_.cluster_name.empty() ? DelegatingRouteEntry::clusterName()
                                           : selection_.cluster_name;
  }
  std::chrono::milliseconds timeout() const override {
    return selection_.timeout.has_value() ? *selection_.timeout : DelegatingRouteEntry::timeout();
  }
  std::optional<std::chrono::milliseconds> idleTimeout() const override {
    return selection_.idle_timeout.has_value() ? selection_.idle_timeout
                                               : DelegatingRouteEntry::idleTimeout();
  }
  uint64_t requestBodyBufferLimit() const override {
    return selection_.request_body_buffer_limit.has_value()
               ? *selection_.request_body_buffer_limit
               : DelegatingRouteEntry::requestBodyBufferLimit();
  }
  Upstream::ResourcePriority priority() const override {
    return selection_.priority.has_value() ? *selection_.priority
                                           : DelegatingRouteEntry::priority();
  }
  Http::Code clusterNotFoundResponseCode() const override {
    return selection_.cluster_not_found_response_code.has_value()
               ? *selection_.cluster_not_found_response_code
               : DelegatingRouteEntry::clusterNotFoundResponseCode();
  }
  const Envoy::Router::RetryPolicyConstSharedPtr& retryPolicy() const override {
    const RouteActionOverride* entry = selection_.route_action_override;
    return entry != nullptr && entry->retry_policy != nullptr ? entry->retry_policy
                                                              : DelegatingRouteEntry::retryPolicy();
  }
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
    const RouteActionOverride* entry = selection_.route_action_override;
    return entry != nullptr && entry->metadata_match_criteria != nullptr
               ? entry->metadata_match_criteria.get()
               : DelegatingRouteEntry::metadataMatchCriteria();
  }
  const std::vector<Envoy::Router::ShadowPolicyPtr>& shadowPolicies() const override {
    const RouteActionOverride* entry = selection_.route_action_override;
    return entry != nullptr && !entry->shadow_policies.empty()
               ? entry->shadow_policies
               : DelegatingRouteEntry::shadowPolicies();
  }
  const Http::HashPolicy* hashPolicy() const override {
    const RouteActionOverride* entry = selection_.route_action_override;
    return entry != nullptr && entry->hash_policy != nullptr ? entry->hash_policy.get()
                                                             : DelegatingRouteEntry::hashPolicy();
  }
  void refreshRouteCluster(const Http::RequestHeaderMap& headers,
                           const StreamInfo::StreamInfo& stream_info) const override;

private:
  const DynamicModuleClusterSpecifierConfigSharedPtr config_;
  const uint64_t random_value_;
  // Only accessed from the worker thread that owns the request, so no synchronization is needed.
  mutable ClusterSpecifierSelection selection_;
};

/**
 * Per-selection context passed to the module as the cluster_specifier_context_envoy_ptr. It bundles
 * the request state the module reads and the selection it is building. Valid only for the duration
 * of a single envoy_dynamic_module_on_cluster_specifier_select call.
 */
struct ClusterSpecifierContext {
  const DynamicModuleClusterSpecifierConfig& config;
  const Http::RequestHeaderMap& headers;
  const StreamInfo::StreamInfo& stream_info;
  const std::string& route_name;
  const uint64_t random_value;
  ClusterSpecifierSelection selection;
};

/**
 * ClusterSpecifierPlugin that delegates cluster selection to a dynamic module.
 */
class DynamicModuleClusterSpecifierPlugin : public Envoy::Router::ClusterSpecifierPlugin {
public:
  explicit DynamicModuleClusterSpecifierPlugin(DynamicModuleClusterSpecifierConfigSharedPtr config)
      : config_(std::move(config)) {}

  // Router::ClusterSpecifierPlugin
  absl::Status validateClusters(const Upstream::ClusterManager& cluster_manager) const override {
    return config_->validateClusters(cluster_manager);
  }
  Envoy::Router::RouteConstSharedPtr route(Envoy::Router::RouteEntryAndRouteConstSharedPtr parent,
                                           const Http::RequestHeaderMap& headers,
                                           const StreamInfo::StreamInfo& stream_info,
                                           uint64_t random_value) const override;

private:
  const DynamicModuleClusterSpecifierConfigSharedPtr config_;
};

} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy
