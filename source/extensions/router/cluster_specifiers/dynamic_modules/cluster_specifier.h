#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/extensions/router/cluster_specifiers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/router/cluster_specifier_plugin.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"
#include "source/common/router/delegating_route_impl.h"
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
};

using RouteActionOverrideMap = absl::flat_hash_map<std::string, RouteActionOverride>;

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
                                      RouteActionOverrideMap route_action_overrides);

  ~DynamicModuleClusterSpecifierConfig();

  /**
   * @param name the key of the entry in the route_action_overrides map.
   * @return the matching override, or nullptr when there is none. The returned pointer is valid for
   * the lifetime of this configuration.
   */
  const RouteActionOverride* routeActionOverride(absl::string_view name) const;

  // The corresponding in-module cluster specifier configuration.
  envoy_dynamic_module_type_cluster_specifier_config_module_ptr in_module_config_{nullptr};

  // The function pointers resolved from the module. All are guaranteed non-nullptr after
  // newDynamicModuleClusterSpecifierConfig() succeeds.
  OnClusterSpecifierConfigDestroyType on_config_destroy_{nullptr};
  OnClusterSpecifierSelectType on_select_{nullptr};

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleClusterSpecifierConfig>>
  newDynamicModuleClusterSpecifierConfig(
      const DynamicModuleClusterSpecifierProto& proto_config,
      Extensions::DynamicModules::DynamicModulePtr dynamic_module,
      Server::Configuration::ServerFactoryContext& context);

  const std::string specifier_name_;
  const std::string specifier_config_;
  const Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
  // Const after construction so that the pointers routeActionOverride() hands out stay valid.
  const RouteActionOverrideMap route_action_overrides_;
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
