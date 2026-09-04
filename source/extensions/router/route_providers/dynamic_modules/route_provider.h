#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/router/route_providers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/hash_policy.h"
#include "envoy/init/manager.h"
#include "envoy/router/route_producer.h"
#include "envoy/router/router.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"
#include "source/common/config/metadata.h"
#include "source/common/router/delegating_route_impl.h"
#include "source/common/router/per_filter_config.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace RouteProviders {
namespace DynamicModules {

using DynamicModuleRouteProviderProto =
    envoy::extensions::router::route_providers::dynamic_modules::v3::DynamicModuleRouteProvider;

// Type aliases for function pointers resolved from the module.
using OnRouteProviderConfigNewType = decltype(&envoy_dynamic_module_on_route_provider_config_new);
using OnRouteProviderConfigDestroyType =
    decltype(&envoy_dynamic_module_on_route_provider_config_destroy);
using OnRouteProviderSelectType = decltype(&envoy_dynamic_module_on_route_provider_select);

/**
 * Route action properties built from a single route_action_overrides entry. These properties are
 * built from extensions and are read by the router through references that must outlive the
 * request, so they are built once at configuration time and selected by name on the request path.
 */
struct RouteActionOverride {
  Envoy::Router::RetryPolicyConstSharedPtr retry_policy;
  Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_criteria;
  std::vector<Envoy::Router::ShadowPolicyPtr> shadow_policies;
  std::unique_ptr<const Http::HashPolicy> hash_policy;
  std::unique_ptr<const Envoy::Router::RateLimitPolicy> rate_limit_policy;
};

using RouteActionOverrideMap = absl::flat_hash_map<std::string, RouteActionOverride>;

/**
 * Per-route configuration properties built from a single per_route_config_overrides entry. Like the
 * route action overrides, they are read by the router and the filters through references that must
 * outlive the request, so they are built once at configuration time and selected by name on the
 * request path.
 */
struct PerRouteConfigOverride {
  std::unique_ptr<Envoy::Router::PerFilterConfigs> per_filter_configs;
  Envoy::Router::RouteTracingConstPtr route_tracing;
};

using PerRouteConfigOverrideMap = absl::flat_hash_map<std::string, PerRouteConfigOverride>;

// How a header setter mutates a request or response header.
enum class HeaderMutationType { Append, Overwrite, Remove };

// A single header mutation the module recorded for a request.
struct HeaderMutation {
  Http::LowerCaseString key;
  std::string value;
  HeaderMutationType type;
};

// A direct response or redirect the module resolved the request to.
struct TerminalResponse {
  bool is_redirect{false};
  Http::Code status_code{};
  // The response body for a direct response, or the Location header value for a redirect.
  std::string body_or_location;
};

/**
 * The route properties a module selected for a request. The selection callbacks fill this in during
 * the select call, and it is applied to the selected route template once the module reports a
 * decision.
 */
struct RouteProviderSelection {
  // The index of the selected route template, unset when the module selected no route.
  std::optional<size_t> selected_index;
  // Empty when the module selected no cluster, in which case the cluster of the route template
  // stays in effect.
  std::string cluster_name;
  std::optional<std::chrono::milliseconds> timeout;
  std::optional<std::chrono::milliseconds> idle_timeout;
  std::optional<std::chrono::milliseconds> max_stream_duration;
  std::optional<uint64_t> request_body_buffer_limit;
  std::optional<Upstream::ResourcePriority> priority;
  // Points into the override map of the route provider configuration, which is immutable after
  // construction.
  const RouteActionOverride* route_action_override{nullptr};
  // Points into the per-route configuration override map of the route provider configuration, which
  // is immutable after construction.
  const PerRouteConfigOverride* per_route_config_override{nullptr};
  // Metadata the module layered onto the route template, keyed by namespace.
  envoy::config::core::v3::Metadata route_metadata;
  std::vector<HeaderMutation> request_header_mutations;
  std::vector<HeaderMutation> response_header_mutations;
  std::optional<std::string> path;
  std::optional<TerminalResponse> terminal;
};

/**
 * Configuration for a dynamic module route provider. It resolves and holds the module symbols, the
 * in-module configuration, the route templates the module selects among and the route action
 * overrides the module may select. It is shared by every route the provider creates so that the
 * module and its configuration outlive all in-flight requests.
 */
class DynamicModuleRouteProviderConfig {
public:
  DynamicModuleRouteProviderConfig(
      absl::string_view provider_name, absl::string_view provider_config,
      Extensions::DynamicModules::DynamicModulePtr dynamic_module,
      RouteActionOverrideMap route_action_overrides,
      PerRouteConfigOverrideMap per_route_config_overrides,
      std::vector<Envoy::Router::RouteEntryAndRouteConstSharedPtr> route_templates);

  ~DynamicModuleRouteProviderConfig();

  /**
   * @param name the key of the entry in the route_action_overrides map.
   * @return the matching override, or nullptr when there is none. The returned pointer is valid for
   * the lifetime of this configuration.
   */
  const RouteActionOverride* routeActionOverride(absl::string_view name) const;

  /**
   * @param name the key of the entry in the per_route_config_overrides map.
   * @return the matching override, or nullptr when there is none. The returned pointer is valid for
   * the lifetime of this configuration.
   */
  const PerRouteConfigOverride* perRouteConfigOverride(absl::string_view name) const;

  const std::vector<Envoy::Router::RouteEntryAndRouteConstSharedPtr>& routeTemplates() const {
    return route_templates_;
  }

  // The corresponding in-module route provider configuration.
  envoy_dynamic_module_type_route_provider_config_module_ptr in_module_config_{nullptr};

  // The function pointers resolved from the module. All are guaranteed non-nullptr after
  // newDynamicModuleRouteProviderConfig() succeeds.
  OnRouteProviderConfigDestroyType on_config_destroy_{nullptr};
  OnRouteProviderSelectType on_select_{nullptr};

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleRouteProviderConfig>>
  newDynamicModuleRouteProviderConfig(
      const DynamicModuleRouteProviderProto& proto_config,
      Extensions::DynamicModules::DynamicModulePtr dynamic_module,
      std::vector<Envoy::Router::RouteEntryAndRouteConstSharedPtr> route_templates,
      Server::Configuration::ServerFactoryContext& context, Init::Manager& init_manager);

  const std::string provider_name_;
  const std::string provider_config_;
  const Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
  // Const after construction so that the pointers routeActionOverride() hands out stay valid.
  const RouteActionOverrideMap route_action_overrides_;
  // Const after construction so that the pointers perRouteConfigOverride() hands out stay valid.
  const PerRouteConfigOverrideMap per_route_config_overrides_;
  const std::vector<Envoy::Router::RouteEntryAndRouteConstSharedPtr> route_templates_;
};

using DynamicModuleRouteProviderConfigSharedPtr = std::shared_ptr<DynamicModuleRouteProviderConfig>;

/**
 * Creates a new DynamicModuleRouteProviderConfig for the given configuration.
 * @param proto_config the route provider configuration.
 * @param dynamic_module the dynamic module to use.
 * @param route_templates the route templates the module selects among.
 * @param context the server factory context used to build the route action overrides.
 * @param init_manager the init manager used to build the per-route configuration overrides.
 * @return a shared pointer to the new config object or an error if symbol resolution, override
 * construction or in-module initialization failed.
 */
absl::StatusOr<DynamicModuleRouteProviderConfigSharedPtr> newDynamicModuleRouteProviderConfig(
    const DynamicModuleRouteProviderProto& proto_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    std::vector<Envoy::Router::RouteEntryAndRouteConstSharedPtr> route_templates,
    Server::Configuration::ServerFactoryContext& context, Init::Manager& init_manager);

// Applies the header mutations the module recorded, in order.
void applyHeaderMutations(Http::HeaderMap& headers, const std::vector<HeaderMutation>& mutations);

// Appends the header mutations the module recorded to the header transforms preview.
void appendHeaderTransforms(Http::HeaderTransforms& transforms,
                            const std::vector<HeaderMutation>& mutations);

// The following overlay a per-route configuration override onto a base route result, so the route
// entry and the terminal route share the same behavior. A property the override sets replaces the
// one of the route template, while a property it leaves unset keeps the one of the route template.

inline const Envoy::Router::RouteTracing*
tracingConfigWithOverride(const PerRouteConfigOverride* entry,
                          const Envoy::Router::RouteTracing* base) {
  return entry != nullptr && entry->route_tracing != nullptr ? entry->route_tracing.get() : base;
}

inline std::optional<bool> filterDisabledWithOverride(const PerRouteConfigOverride* entry,
                                                      absl::string_view name,
                                                      std::optional<bool> base) {
  if (entry != nullptr && entry->per_filter_configs != nullptr) {
    if (const std::optional<bool> disabled = entry->per_filter_configs->disabled(name);
        disabled.has_value()) {
      return disabled;
    }
  }
  return base;
}

inline const Envoy::Router::RouteSpecificFilterConfig*
mostSpecificPerFilterConfigWithOverride(const PerRouteConfigOverride* entry, absl::string_view name,
                                        const Envoy::Router::RouteSpecificFilterConfig* base) {
  if (entry != nullptr && entry->per_filter_configs != nullptr) {
    if (const auto* config = entry->per_filter_configs->get(name); config != nullptr) {
      return config;
    }
  }
  return base;
}

inline Envoy::Router::RouteSpecificFilterConfigs
perFilterConfigsWithOverride(const PerRouteConfigOverride* entry, absl::string_view name,
                             Envoy::Router::RouteSpecificFilterConfigs base) {
  if (entry != nullptr && entry->per_filter_configs != nullptr) {
    if (const auto* config = entry->per_filter_configs->get(name); config != nullptr) {
      base.push_back(config);
    }
  }
  return base;
}

/**
 * Route entry that delegates to the selected route template and applies the properties the dynamic
 * module selected for the request. It is created per request and used by a single worker thread,
 * and the selection is fixed at creation, so no synchronization is needed.
 */
class DynamicModuleRouteProviderRouteEntry : public Envoy::Router::DelegatingRouteEntry {
public:
  DynamicModuleRouteProviderRouteEntry(
      Envoy::Router::RouteEntryAndRouteConstSharedPtr parent,
      DynamicModuleRouteProviderConfigSharedPtr config, RouteProviderSelection selection,
      Envoy::Config::MetadataPackPtr<Envoy::Router::HttpRouteTypedMetadataFactory> metadata_pack)
      : DelegatingRouteEntry(std::move(parent)), config_(std::move(config)),
        selection_(std::move(selection)), metadata_pack_(std::move(metadata_pack)) {}

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
  std::optional<std::chrono::milliseconds> maxStreamDuration() const override {
    return selection_.max_stream_duration.has_value() ? selection_.max_stream_duration
                                                      : DelegatingRouteEntry::maxStreamDuration();
  }
  bool usingNewTimeouts() const override {
    return selection_.max_stream_duration.has_value() || DelegatingRouteEntry::usingNewTimeouts();
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
  const Http::HashPolicy* hashPolicy() const override {
    const RouteActionOverride* entry = selection_.route_action_override;
    return entry != nullptr && entry->hash_policy != nullptr ? entry->hash_policy.get()
                                                             : DelegatingRouteEntry::hashPolicy();
  }
  const Envoy::Router::RateLimitPolicy& rateLimitPolicy() const override {
    const RouteActionOverride* entry = selection_.route_action_override;
    return entry != nullptr && entry->rate_limit_policy != nullptr
               ? *entry->rate_limit_policy
               : DelegatingRouteEntry::rateLimitPolicy();
  }
  const envoy::config::core::v3::Metadata& metadata() const override {
    return metadata_pack_ != nullptr ? metadata_pack_->proto_metadata_
                                     : DelegatingRouteEntry::metadata();
  }
  const Envoy::Config::TypedMetadata& typedMetadata() const override {
    return metadata_pack_ != nullptr ? metadata_pack_->typed_metadata_
                                     : DelegatingRouteEntry::typedMetadata();
  }
  std::string currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers,
                                         const Formatter::Context& context,
                                         const StreamInfo::StreamInfo& stream_info) const override {
    return selection_.path.has_value()
               ? *selection_.path
               : DelegatingRouteEntry::currentUrlPathAfterRewrite(headers, context, stream_info);
  }
  void finalizeRequestHeaders(Http::RequestHeaderMap& headers, const Formatter::Context& context,
                              const StreamInfo::StreamInfo& stream_info,
                              bool insert_envoy_original_path) const override {
    DelegatingRouteEntry::finalizeRequestHeaders(headers, context, stream_info,
                                                 insert_envoy_original_path);
    // The module supplies the full path, so it is set verbatim. The template rewrite runs first, so
    // x-envoy-original-path follows the template configuration rather than this override.
    if (selection_.path.has_value()) {
      headers.setPath(*selection_.path);
    }
    applyHeaderMutations(headers, selection_.request_header_mutations);
  }
  Http::HeaderTransforms requestHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                 bool do_formatting = true) const override {
    Http::HeaderTransforms transforms =
        DelegatingRouteEntry::requestHeaderTransforms(stream_info, do_formatting);
    appendHeaderTransforms(transforms, selection_.request_header_mutations);
    return transforms;
  }
  void finalizeResponseHeaders(Http::ResponseHeaderMap& headers, const Formatter::Context& context,
                               const StreamInfo::StreamInfo& stream_info) const override {
    DelegatingRouteEntry::finalizeResponseHeaders(headers, context, stream_info);
    applyHeaderMutations(headers, selection_.response_header_mutations);
  }
  Http::HeaderTransforms responseHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                  bool do_formatting = true) const override {
    Http::HeaderTransforms transforms =
        DelegatingRouteEntry::responseHeaderTransforms(stream_info, do_formatting);
    appendHeaderTransforms(transforms, selection_.response_header_mutations);
    return transforms;
  }

  // Router::Route
  const Envoy::Router::RouteTracing* tracingConfig() const override {
    return tracingConfigWithOverride(selection_.per_route_config_override,
                                     DelegatingRouteEntry::tracingConfig());
  }
  std::optional<bool> filterDisabled(absl::string_view config_name) const override {
    return filterDisabledWithOverride(selection_.per_route_config_override, config_name,
                                      DelegatingRouteEntry::filterDisabled(config_name));
  }
  const Envoy::Router::RouteSpecificFilterConfig*
  mostSpecificPerFilterConfig(absl::string_view name) const override {
    return mostSpecificPerFilterConfigWithOverride(
        selection_.per_route_config_override, name,
        DelegatingRouteEntry::mostSpecificPerFilterConfig(name));
  }
  Envoy::Router::RouteSpecificFilterConfigs
  perFilterConfigs(absl::string_view name) const override {
    return perFilterConfigsWithOverride(selection_.per_route_config_override, name,
                                        DelegatingRouteEntry::perFilterConfigs(name));
  }

private:
  const DynamicModuleRouteProviderConfigSharedPtr config_;
  const RouteProviderSelection selection_;
  // Built once at creation so the reference metadata() and typedMetadata() return stays valid. Null
  // when the module set no metadata, so both accessors fall back to the route template.
  const Envoy::Config::MetadataPackPtr<Envoy::Router::HttpRouteTypedMetadataFactory> metadata_pack_;
};

/**
 * Direct response entry the router honors when the module resolved a request to a direct response
 * or a redirect.
 */
class DynamicModuleRouteProviderDirectResponseEntry : public Envoy::Router::DirectResponseEntry {
public:
  DynamicModuleRouteProviderDirectResponseEntry(
      TerminalResponse terminal, std::vector<HeaderMutation> response_header_mutations)
      : terminal_(std::move(terminal)),
        response_header_mutations_(std::move(response_header_mutations)) {}

  // Router::DirectResponseEntry
  Http::Code responseCode() const override { return terminal_.status_code; }
  std::string newUri(const Http::RequestHeaderMap&, const StreamInfo::StreamInfo&) const override {
    return terminal_.is_redirect ? terminal_.body_or_location : "";
  }
  absl::string_view formatBody(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                               const StreamInfo::StreamInfo&,
                               std::string& body_out) const override {
    if (!terminal_.is_redirect) {
      body_out = terminal_.body_or_location;
    }
    return body_out;
  }
  absl::string_view responseContentType() const override { return ""; }
  void rewritePathHeader(Http::RequestHeaderMap&, bool) const override {}

  // Router::ResponseEntry
  void finalizeResponseHeaders(Http::ResponseHeaderMap& headers, const Formatter::Context&,
                               const StreamInfo::StreamInfo&) const override {
    applyHeaderMutations(headers, response_header_mutations_);
  }
  Http::HeaderTransforms responseHeaderTransforms(const StreamInfo::StreamInfo&,
                                                  bool) const override {
    Http::HeaderTransforms transforms;
    appendHeaderTransforms(transforms, response_header_mutations_);
    return transforms;
  }

private:
  const TerminalResponse terminal_;
  const std::vector<HeaderMutation> response_header_mutations_;
};

/**
 * Route the provider returns when the module resolved the request to a direct response or a
 * redirect. It wraps the route template so the per-filter config and metadata of the template stay
 * in effect, and reports the direct response entry the router honors before touching a cluster.
 */
class DynamicModuleRouteProviderTerminalRoute : public Envoy::Router::DelegatingRoute {
public:
  DynamicModuleRouteProviderTerminalRoute(Envoy::Router::RouteConstSharedPtr base,
                                          DynamicModuleRouteProviderConfigSharedPtr config,
                                          const PerRouteConfigOverride* per_route_config_override,
                                          TerminalResponse terminal,
                                          std::vector<HeaderMutation> response_header_mutations)
      : Envoy::Router::DelegatingRoute(std::move(base)), config_(std::move(config)),
        per_route_config_override_(per_route_config_override),
        direct_response_(std::move(terminal), std::move(response_header_mutations)) {}

  const Envoy::Router::DirectResponseEntry* directResponseEntry() const override {
    return &direct_response_;
  }
  // A terminal route resolves to a direct response, so it reports no route entry, the same as a
  // route configured with a direct response or a redirect.
  const Envoy::Router::RouteEntry* routeEntry() const override { return nullptr; }
  const Envoy::Router::RouteTracing* tracingConfig() const override {
    return tracingConfigWithOverride(per_route_config_override_,
                                     Envoy::Router::DelegatingRoute::tracingConfig());
  }
  std::optional<bool> filterDisabled(absl::string_view config_name) const override {
    return filterDisabledWithOverride(per_route_config_override_, config_name,
                                      Envoy::Router::DelegatingRoute::filterDisabled(config_name));
  }
  const Envoy::Router::RouteSpecificFilterConfig*
  mostSpecificPerFilterConfig(absl::string_view name) const override {
    return mostSpecificPerFilterConfigWithOverride(
        per_route_config_override_, name,
        Envoy::Router::DelegatingRoute::mostSpecificPerFilterConfig(name));
  }
  Envoy::Router::RouteSpecificFilterConfigs
  perFilterConfigs(absl::string_view name) const override {
    return perFilterConfigsWithOverride(per_route_config_override_, name,
                                        Envoy::Router::DelegatingRoute::perFilterConfigs(name));
  }

private:
  // Held so the per-route configuration override the module selected outlives the request.
  const DynamicModuleRouteProviderConfigSharedPtr config_;
  const PerRouteConfigOverride* const per_route_config_override_;
  const DynamicModuleRouteProviderDirectResponseEntry direct_response_;
};

/**
 * Per-selection context passed to the module as the route_provider_context_envoy_ptr. It bundles
 * the request state the module reads and the selection it is building. Valid only for the duration
 * of a single envoy_dynamic_module_on_route_provider_select call.
 */
struct RouteProviderContext {
  const DynamicModuleRouteProviderConfig& config;
  const Http::RequestHeaderMap& headers;
  const StreamInfo::StreamInfo& stream_info;
  const uint64_t random_value;
  RouteProviderSelection selection;
};

/**
 * RouteProducer that delegates route selection and per-request overrides to a dynamic module.
 */
class DynamicModuleRouteProvider : public Envoy::Router::RouteProducer,
                                   public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  explicit DynamicModuleRouteProvider(DynamicModuleRouteProviderConfigSharedPtr config)
      : config_(std::move(config)) {}

  // Router::RouteProducer
  Envoy::Router::RouteConstSharedPtr produceRoute(const Envoy::Router::RouteCallback& cb,
                                                  const Http::RequestHeaderMap& headers,
                                                  const StreamInfo::StreamInfo& stream_info,
                                                  uint64_t random_value) const override;

private:
  const DynamicModuleRouteProviderConfigSharedPtr config_;
};

} // namespace DynamicModules
} // namespace RouteProviders
} // namespace Router
} // namespace Extensions
} // namespace Envoy
