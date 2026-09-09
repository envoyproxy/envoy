#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/router/route_extension/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/http/hash_policy.h"
#include "envoy/router/route_extension.h"
#include "envoy/server/factory_context.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/router/delegating_route_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace DynamicModules {

using DynamicModuleRouteExtensionProto =
    envoy::extensions::router::route_extension::dynamic_modules::v3::DynamicModuleRouteExtension;

// Type aliases for the function pointers resolved from the module.
using OnRouteExtensionConfigNewType = decltype(&envoy_dynamic_module_on_route_extension_config_new);
using OnRouteExtensionConfigDestroyType =
    decltype(&envoy_dynamic_module_on_route_extension_config_destroy);
using OnRouteExtensionOnRouteType = decltype(&envoy_dynamic_module_on_route_extension_on_route);

// Route action properties built from a single route_action_overrides entry. These properties are
// built from extensions and are read by the router through references that must outlive the
// request, so they are built once at configuration time and selected by name on the request path.
struct RouteActionOverride {
  Envoy::Router::RetryPolicyConstSharedPtr retry_policy;
  Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_criteria;
  std::vector<Envoy::Router::ShadowPolicyPtr> shadow_policies;
  std::unique_ptr<Http::HashPolicy> hash_policy;
};

using RouteActionOverrideMap = absl::flat_hash_map<std::string, RouteActionOverride>;

// The configuration shared by every request handled by a dynamic module route extension. It owns
// the loaded module, the in-module configuration and the route action overrides the module may
// select.
class DynamicModuleRouteExtensionConfig {
public:
  DynamicModuleRouteExtensionConfig(absl::string_view extension_name,
                                    absl::string_view extension_config,
                                    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                    RouteActionOverrideMap route_action_overrides);
  ~DynamicModuleRouteExtensionConfig();

  // The override with the given name, or nullptr when there is none. The returned pointer is valid
  // for the lifetime of this configuration.
  const RouteActionOverride* routeActionOverride(absl::string_view name) const;

  // Validates the statically named mirror clusters of every route action override.
  absl::Status validateClusters(const Upstream::ClusterManager& cluster_manager) const;

  // The corresponding in-module route extension configuration.
  envoy_dynamic_module_type_route_extension_config_module_ptr in_module_config_{nullptr};

  // The function pointers resolved from the module. Both are guaranteed non-nullptr after
  // newDynamicModuleRouteExtensionConfig() succeeds.
  OnRouteExtensionConfigDestroyType on_config_destroy_{nullptr};
  OnRouteExtensionOnRouteType on_route_{nullptr};

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleRouteExtensionConfig>>
  newDynamicModuleRouteExtensionConfig(const DynamicModuleRouteExtensionProto& proto_config,
                                       Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                       Server::Configuration::ServerFactoryContext& context);

  const std::string extension_name_;
  const std::string extension_config_;
  const Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
  // Const after construction so that the pointers routeActionOverride() hands out stay valid.
  const RouteActionOverrideMap route_action_overrides_;
};

using DynamicModuleRouteExtensionConfigSharedPtr =
    std::shared_ptr<DynamicModuleRouteExtensionConfig>;

// Create a route extension configuration by resolving the module symbols and initializing the
// in-module configuration.
absl::StatusOr<DynamicModuleRouteExtensionConfigSharedPtr>
newDynamicModuleRouteExtensionConfig(const DynamicModuleRouteExtensionProto& proto_config,
                                     Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                     Server::Configuration::ServerFactoryContext& context);

// The overrides a module records for a request during the on_route hook.
struct RouteExtensionOverrides {
  std::string cluster_name;
  // Points into the override map of the configuration, which is immutable after construction. Null
  // when the module selected none.
  const RouteActionOverride* route_action_override{nullptr};
};

// The per-request context handed to the module during the on_route hook. It is valid only for the
// duration of the call.
struct RouteExtensionContext {
  const DynamicModuleRouteExtensionConfig& config;
  const Http::RequestHeaderMap& headers;
  const StreamInfo::StreamInfo& stream_info;
  const uint64_t random_value;
  RouteExtensionOverrides overrides;
};

// Route entry that delegates to the matched route and applies the cluster and route action override
// a module selected for a request. It is built once when the chain resolves the route and is not
// refreshed, so the selection is fixed for the request. It holds the configuration so the override
// it points at outlives the request.
class DynamicModuleRouteEntry : public Envoy::Router::DelegatingRouteEntry {
public:
  DynamicModuleRouteEntry(Envoy::Router::RouteConstSharedPtr parent,
                          DynamicModuleRouteExtensionConfigSharedPtr config,
                          std::string cluster_name,
                          const RouteActionOverride* route_action_override)
      : DelegatingRouteEntry(std::move(parent)), config_(std::move(config)),
        cluster_name_(std::move(cluster_name)), route_action_override_(route_action_override) {}

  // Router::RouteEntry
  const std::string& clusterName() const override {
    return cluster_name_.empty() ? DelegatingRouteEntry::clusterName() : cluster_name_;
  }
  const Envoy::Router::RetryPolicyConstSharedPtr& retryPolicy() const override {
    return route_action_override_ != nullptr && route_action_override_->retry_policy != nullptr
               ? route_action_override_->retry_policy
               : DelegatingRouteEntry::retryPolicy();
  }
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
    return route_action_override_ != nullptr &&
                   route_action_override_->metadata_match_criteria != nullptr
               ? route_action_override_->metadata_match_criteria.get()
               : DelegatingRouteEntry::metadataMatchCriteria();
  }
  const std::vector<Envoy::Router::ShadowPolicyPtr>& shadowPolicies() const override {
    return route_action_override_ != nullptr && !route_action_override_->shadow_policies.empty()
               ? route_action_override_->shadow_policies
               : DelegatingRouteEntry::shadowPolicies();
  }
  const Http::HashPolicy* hashPolicy() const override {
    return route_action_override_ != nullptr && route_action_override_->hash_policy != nullptr
               ? route_action_override_->hash_policy.get()
               : DelegatingRouteEntry::hashPolicy();
  }

private:
  const DynamicModuleRouteExtensionConfigSharedPtr config_;
  const std::string cluster_name_;
  const RouteActionOverride* const route_action_override_;
};

// A route extension backed by a dynamic module.
class DynamicModuleRouteExtension : public Envoy::Router::RouteExtension {
public:
  explicit DynamicModuleRouteExtension(DynamicModuleRouteExtensionConfigSharedPtr config)
      : config_(std::move(config)) {}

  // Router::RouteExtension
  Envoy::Router::RouteConstSharedPtr onRoute(Envoy::Router::RouteConstSharedPtr route,
                                             const Http::RequestHeaderMap& headers,
                                             const StreamInfo::StreamInfo& stream_info,
                                             uint64_t random_value) const override;
  absl::Status validateClusters(const Upstream::ClusterManager& cluster_manager) const override {
    return config_->validateClusters(cluster_manager);
  }

private:
  const DynamicModuleRouteExtensionConfigSharedPtr config_;
};

} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy
