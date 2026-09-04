#include "source/extensions/router/route_providers/dynamic_modules/route_provider.h"

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
#include "source/common/router/router_ratelimit.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace RouteProviders {
namespace DynamicModules {
namespace {

using RouteActionOverrideProto =
    envoy::extensions::router::route_providers::dynamic_modules::v3::RouteActionOverride;
using PerRouteConfigOverrideProto =
    envoy::extensions::router::route_providers::dynamic_modules::v3::PerRouteConfigOverride;

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
  if (!proto_override.rate_limits().empty()) {
    absl::Status creation_status = absl::OkStatus();
    auto policy = std::make_unique<Envoy::Router::RateLimitPolicyImpl>(proto_override.rate_limits(),
                                                                       context, creation_status);
    RETURN_IF_NOT_OK(creation_status);
    entry.rate_limit_policy = std::move(policy);
  }
  // Validate what was built rather than what was configured, so an entry that replaces no property
  // is rejected at configuration load rather than accepted as a decision on the request path.
  if (entry.retry_policy == nullptr && entry.metadata_match_criteria == nullptr &&
      entry.shadow_policies.empty() && entry.hash_policy == nullptr &&
      entry.rate_limit_policy == nullptr) {
    return absl::InvalidArgumentError(
        "Route action override must replace at least one route action property");
  }
  return entry;
}

absl::StatusOr<PerRouteConfigOverride>
buildPerRouteConfigOverride(const PerRouteConfigOverrideProto& proto_override,
                            Server::Configuration::ServerFactoryContext& context,
                            Init::Manager& init_manager) {
  PerRouteConfigOverride entry;
  if (!proto_override.typed_per_filter_config().empty()) {
    auto configs_or_error =
        Envoy::Router::PerFilterConfigs::create(proto_override.typed_per_filter_config(), context,
                                                context.messageValidationVisitor(), init_manager);
    RETURN_IF_NOT_OK_REF(configs_or_error.status());
    entry.per_filter_configs = std::move(configs_or_error.value());
  }
  if (proto_override.has_tracing()) {
    // RouteTracingImpl builds custom tags from extensions, which can throw on module-supplied
    // input, so translate a failure into a configuration load error rather than let it escape.
    TRY_NEEDS_AUDIT {
      entry.route_tracing =
          std::make_unique<Envoy::Router::RouteTracingImpl>(proto_override.tracing());
    }
    END_TRY
    CATCH(const EnvoyException& e, {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to build per-route configuration override tracing: ", e.what()));
    });
  }
  // Validate what was built rather than what was configured, so an entry that replaces no property
  // is rejected at configuration load rather than accepted as a decision on the request path.
  if (entry.per_filter_configs == nullptr && entry.route_tracing == nullptr) {
    return absl::InvalidArgumentError(
        "Per-route configuration override must replace at least one property");
  }
  return entry;
}

// Builds the metadata the module layered onto the route template. It merges per namespace so an
// entry replaces only its own key while the other keys of the template stay in effect. Returns null
// when the module set no metadata, so the accessors fall back to the template.
Envoy::Config::MetadataPackPtr<Envoy::Router::HttpRouteTypedMetadataFactory>
buildRouteMetadataPack(const Envoy::Router::RouteEntryAndRouteConstSharedPtr& base,
                       const envoy::config::core::v3::Metadata& route_metadata) {
  if (route_metadata.filter_metadata().empty() && route_metadata.typed_filter_metadata().empty()) {
    return nullptr;
  }
  envoy::config::core::v3::Metadata merged = base->metadata();
  for (const auto& [name, fields] : route_metadata.filter_metadata()) {
    (*merged.mutable_filter_metadata())[name].MergeFrom(fields);
  }
  for (const auto& [name, typed] : route_metadata.typed_filter_metadata()) {
    (*merged.mutable_typed_filter_metadata())[name] = typed;
  }
  // Building the pack runs the registered typed metadata factories, which can throw on
  // module-supplied input, so fall back to the template rather than let it reach the worker.
  TRY_NEEDS_AUDIT { return std::make_unique<Envoy::Router::RouteMetadataPack>(merged); }
  END_TRY
  CATCH(const EnvoyException& e, {
    ENVOY_LOG_EVERY_POW_2_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "dynamic module route metadata rejected by a typed metadata factory, using the route "
        "template instead: {}",
        e.what());
    return nullptr;
  });
}

} // namespace

void applyHeaderMutations(Http::HeaderMap& headers, const std::vector<HeaderMutation>& mutations) {
  for (const auto& mutation : mutations) {
    switch (mutation.type) {
    case HeaderMutationType::Append:
      headers.addCopy(mutation.key, mutation.value);
      break;
    case HeaderMutationType::Overwrite:
      headers.setCopy(mutation.key, mutation.value);
      break;
    case HeaderMutationType::Remove:
      headers.remove(mutation.key);
      break;
    }
  }
}

void appendHeaderTransforms(Http::HeaderTransforms& transforms,
                            const std::vector<HeaderMutation>& mutations) {
  for (const auto& mutation : mutations) {
    switch (mutation.type) {
    case HeaderMutationType::Append:
      transforms.headers_to_append_or_add.emplace_back(mutation.key, mutation.value);
      break;
    case HeaderMutationType::Overwrite:
      transforms.headers_to_overwrite_or_add.emplace_back(mutation.key, mutation.value);
      break;
    case HeaderMutationType::Remove:
      transforms.headers_to_remove.push_back(mutation.key);
      break;
    }
  }
}

DynamicModuleRouteProviderConfig::DynamicModuleRouteProviderConfig(
    absl::string_view provider_name, absl::string_view provider_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    RouteActionOverrideMap route_action_overrides,
    PerRouteConfigOverrideMap per_route_config_overrides,
    std::vector<Envoy::Router::RouteEntryAndRouteConstSharedPtr> route_templates)
    : provider_name_(provider_name), provider_config_(provider_config),
      dynamic_module_(std::move(dynamic_module)),
      route_action_overrides_(std::move(route_action_overrides)),
      per_route_config_overrides_(std::move(per_route_config_overrides)),
      route_templates_(std::move(route_templates)) {}

DynamicModuleRouteProviderConfig::~DynamicModuleRouteProviderConfig() {
  if (in_module_config_ != nullptr) {
    // The destroy hook is resolved before the in-module configuration is created.
    ASSERT(on_config_destroy_ != nullptr);
    on_config_destroy_(in_module_config_);
  }
}

const RouteActionOverride*
DynamicModuleRouteProviderConfig::routeActionOverride(absl::string_view name) const {
  const auto it = route_action_overrides_.find(name);
  return it != route_action_overrides_.end() ? &it->second : nullptr;
}

const PerRouteConfigOverride*
DynamicModuleRouteProviderConfig::perRouteConfigOverride(absl::string_view name) const {
  const auto it = per_route_config_overrides_.find(name);
  return it != per_route_config_overrides_.end() ? &it->second : nullptr;
}

absl::StatusOr<DynamicModuleRouteProviderConfigSharedPtr> newDynamicModuleRouteProviderConfig(
    const DynamicModuleRouteProviderProto& proto_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    std::vector<Envoy::Router::RouteEntryAndRouteConstSharedPtr> route_templates,
    Server::Configuration::ServerFactoryContext& context, Init::Manager& init_manager) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  auto on_config_new = dynamic_module->getFunctionPointer<OnRouteProviderConfigNewType>(
      "envoy_dynamic_module_on_route_provider_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnRouteProviderConfigDestroyType>(
      "envoy_dynamic_module_on_route_provider_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_select = dynamic_module->getFunctionPointer<OnRouteProviderSelectType>(
      "envoy_dynamic_module_on_route_provider_select");
  RETURN_IF_NOT_OK_REF(on_select.status());

  // Use knownAnyToBytes() to properly handle StringValue/BytesValue/Struct types.
  std::string provider_config;
  if (proto_config.has_provider_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.provider_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    provider_config = std::move(config_or_error.value());
  }

  RouteActionOverrideMap route_action_overrides;
  route_action_overrides.reserve(proto_config.route_action_overrides().size());
  for (const auto& [name, proto_override] : proto_config.route_action_overrides()) {
    auto entry_or_error = buildRouteActionOverride(proto_override, context);
    RETURN_IF_NOT_OK_REF(entry_or_error.status());
    route_action_overrides.emplace(name, std::move(entry_or_error.value()));
  }

  PerRouteConfigOverrideMap per_route_config_overrides;
  per_route_config_overrides.reserve(proto_config.per_route_config_overrides().size());
  for (const auto& [name, proto_override] : proto_config.per_route_config_overrides()) {
    auto entry_or_error = buildPerRouteConfigOverride(proto_override, context, init_manager);
    RETURN_IF_NOT_OK_REF(entry_or_error.status());
    per_route_config_overrides.emplace(name, std::move(entry_or_error.value()));
  }

  auto config = std::make_shared<DynamicModuleRouteProviderConfig>(
      proto_config.provider_name(), provider_config, std::move(dynamic_module),
      std::move(route_action_overrides), std::move(per_route_config_overrides),
      std::move(route_templates));
  config->on_config_destroy_ = on_config_destroy.value();
  config->on_select_ = on_select.value();

  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = config->provider_name_.data(),
                                                     .length = config->provider_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buf = {.ptr = config->provider_config_.data(),
                                                       .length = config->provider_config_.size()};
  config->in_module_config_ = (*on_config_new.value())(static_cast<void*>(config.get()), name_buf,
                                                       config_buf, config->route_templates_.size());

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module route provider config");
  }
  return config;
}

Envoy::Router::RouteConstSharedPtr DynamicModuleRouteProvider::produceRoute(
    const Envoy::Router::RouteCallback& cb, const Http::RequestHeaderMap& headers,
    const StreamInfo::StreamInfo& stream_info, uint64_t random_value) const {
  RouteProviderContext context{*config_, headers, stream_info, random_value, {}};
  if (!config_->on_select_(config_->in_module_config_, static_cast<void*>(&context))) {
    ENVOY_LOG(debug, "dynamic module route provider selected no route");
    return nullptr;
  }
  RouteProviderSelection& selection = context.selection;
  if (!selection.selected_index.has_value()) {
    ENVOY_LOG(debug, "dynamic module route provider reported a decision without selecting a route");
    return nullptr;
  }
  const auto& route_templates = config_->routeTemplates();
  // select_route validates the index against the template count, so this only guards a module that
  // reaches the setter directly.
  if (*selection.selected_index >= route_templates.size()) {
    IS_ENVOY_BUG("dynamic module route provider selected an out of range route index");
    return nullptr;
  }
  const Envoy::Router::RouteEntryAndRouteConstSharedPtr& base =
      route_templates[*selection.selected_index];

  Envoy::Router::RouteConstSharedPtr route;
  if (selection.terminal.has_value()) {
    route = std::make_shared<DynamicModuleRouteProviderTerminalRoute>(
        base, config_, selection.per_route_config_override, std::move(*selection.terminal),
        std::move(selection.response_header_mutations));
  } else if (base->directResponseEntry() != nullptr) {
    // The route template is itself a direct response or redirect, so honor it as is. A route entry
    // override would assert on it, and its cluster overrides do not apply.
    route = base;
  } else {
    auto metadata_pack = buildRouteMetadataPack(base, selection.route_metadata);
    route = std::make_shared<DynamicModuleRouteProviderRouteEntry>(
        base, config_, std::move(selection), std::move(metadata_pack));
  }

  if (cb == nullptr) {
    return route;
  }
  return cb(route, Envoy::Router::RouteEvalStatus::NoMoreRoutes) ==
                 Envoy::Router::RouteMatchStatus::Accept
             ? route
             : nullptr;
}

} // namespace DynamicModules
} // namespace RouteProviders
} // namespace Router
} // namespace Extensions
} // namespace Envoy
