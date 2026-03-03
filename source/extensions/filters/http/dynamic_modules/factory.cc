#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/http/dynamic_modules/filter.h"
#include "source/extensions/filters/http/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

absl::StatusOr<Http::FilterFactoryCb> DynamicModuleConfigFactory::createFilterFactory(
    const FilterConfig& proto_config, const std::string&,
    Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope,
    Init::Manager* init_manager) {

  const auto& module_config = proto_config.dynamic_module_config();

  // Remote source requires async warming — handle separately.
  if (module_config.has_module() && module_config.module().has_remote()) {
    return createFilterFactoryFromRemoteSource(proto_config, context, scope, init_manager);
  }

  // Load the module: either from a local file path or by name.
  absl::StatusOr<Extensions::DynamicModules::DynamicModulePtr> dynamic_module;
  if (module_config.has_module()) {
    if (!module_config.module().has_local() || !module_config.module().local().has_filename()) {
      return absl::InvalidArgumentError("Only local.filename is supported for module sources; "
                                        "inline_bytes and inline_string are not supported");
    }
    dynamic_module = Extensions::DynamicModules::newDynamicModule(
        module_config.module().local().filename(), module_config.do_not_close(),
        module_config.load_globally());
  } else {
    if (module_config.name().empty()) {
      return absl::InvalidArgumentError(
          "Either 'name' or 'module' must be specified in dynamic_module_config");
    }
    dynamic_module = Extensions::DynamicModules::newDynamicModuleByName(
        module_config.name(), module_config.do_not_close(), module_config.load_globally());
  }
  if (!dynamic_module.ok()) {
    return absl::InvalidArgumentError("Failed to load dynamic module: " +
                                      std::string(dynamic_module.status().message()));
  }

  std::string config;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.filter_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    config = std::move(config_or_error.value());
  }

  // Use configured metrics namespace or fall back to the default.
  const std::string metrics_namespace =
      module_config.metrics_namespace().empty()
          ? std::string(Extensions::DynamicModules::HttpFilters::DefaultMetricsNamespace)
          : module_config.metrics_namespace();

  absl::StatusOr<
      Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfigSharedPtr>
      filter_config =
          Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
              proto_config.filter_name(), config, metrics_namespace, proto_config.terminal_filter(),
              std::move(dynamic_module.value()), scope, context);

  if (!filter_config.ok()) {
    return absl::InvalidArgumentError("Failed to create filter config: " +
                                      std::string(filter_config.status().message()));
  }

  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
    context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
  }

  return [config = filter_config.value()](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    const std::string& worker_name = callbacks.dispatcher().name();
    auto pos = worker_name.find_first_of('_');
    ENVOY_BUG(pos != std::string::npos, "worker name is not in expected format worker_{index}");
    uint32_t worker_index;
    if (!absl::SimpleAtoi(worker_name.substr(pos + 1), &worker_index)) {
      IS_ENVOY_BUG("failed to parse worker index from name");
    }
    auto filter =
        std::make_shared<Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilter>(
            config, config->stats_scope_->symbolTable(), worker_index);
    callbacks.addStreamFilter(filter);
    // addStreamFilter() sets decoder/encoder filter callbacks. Initialize the in-module filter
    // after so it can access all necessary context during creation.
    filter->initializeInModuleFilter();
  };
}

absl::StatusOr<Http::FilterFactoryCb>
DynamicModuleConfigFactory::createFilterFactoryFromRemoteSource(
    const FilterConfig& proto_config, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope& scope, Init::Manager* init_manager) {

  const auto& module_config = proto_config.dynamic_module_config();
  const auto& remote_source = module_config.module().remote();
  const std::string& sha256_hash = remote_source.sha256();

  if (sha256_hash.empty()) {
    return absl::InvalidArgumentError("SHA256 hash is required for remote module sources");
  }

  if (init_manager == nullptr) {
    return absl::InvalidArgumentError(
        "Remote module sources require an init manager for warming and are not supported via "
        "the server context factory path (createFilterFactoryFromProtoWithServerContext). "
        "Use the listener-level filter config instead");
  }

  const std::string metrics_namespace =
      module_config.metrics_namespace().empty()
          ? std::string(Extensions::DynamicModules::HttpFilters::DefaultMetricsNamespace)
          : module_config.metrics_namespace();

  // AsyncLoadState is shared between the fetch callback (which populates filter_config)
  // and the returned factory callback (which reads it). Also owns the RemoteAsyncDataProvider
  // to prevent it from being destroyed before the fetch completes.
  struct AsyncLoadState {
    Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfigSharedPtr filter_config;
    RemoteAsyncDataProviderPtr remote_provider;
  };
  auto state = std::make_shared<AsyncLoadState>();

  // SHA256 verification is handled by the underlying RemoteDataFetcher.
  // Use a weak_ptr in the callback to guard against the callback firing after the
  // factory lambda (which owns `state` via shared_ptr) has been destroyed.
  std::weak_ptr<AsyncLoadState> weak_state = state;
  state->remote_provider = std::make_unique<RemoteAsyncDataProvider>(
      context.clusterManager(), *init_manager, remote_source, context.mainThreadDispatcher(),
      context.api().randomGenerator(), false,
      [weak_state, sha256_hash, proto_config_copy = proto_config, &context, &scope,
       metrics_namespace](const std::string& data) {
        if (data.empty()) {
          ENVOY_LOG_MISC(warn, "Remote dynamic module fetch failed for SHA256 {}", sha256_hash);
          return;
        }
        auto state = weak_state.lock();
        if (!state) {
          return;
        }

        const auto& module_config = proto_config_copy.dynamic_module_config();
        auto dynamic_module = Extensions::DynamicModules::newDynamicModuleFromBytes(
            data, sha256_hash, module_config.do_not_close(), module_config.load_globally());
        if (!dynamic_module.ok()) {
          ENVOY_LOG_MISC(warn, "Remote dynamic module fetched but failed to load for SHA256 {}: {}",
                         sha256_hash, dynamic_module.status().message());
          return;
        }

        std::string config;
        if (proto_config_copy.has_filter_config()) {
          auto config_or_error = MessageUtil::anyToBytes(proto_config_copy.filter_config());
          if (!config_or_error.ok()) {
            ENVOY_LOG_MISC(warn, "Failed to parse filter config for SHA256 {}: {}", sha256_hash,
                           config_or_error.status().message());
            return;
          }
          config = std::move(config_or_error.value());
        }

        auto filter_config =
            Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
                proto_config_copy.filter_name(), config, metrics_namespace,
                proto_config_copy.terminal_filter(), std::move(dynamic_module.value()), scope,
                context);
        if (!filter_config.ok()) {
          ENVOY_LOG_MISC(warn,
                         "Remote dynamic module loaded but failed to create config for "
                         "SHA256 {}: {}",
                         sha256_hash, filter_config.status().message());
          return;
        }
        state->filter_config = filter_config.value();
        if (Runtime::runtimeFeatureEnabled(
                "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
          context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
        }
      });

  // If the fetch failed, filter_config will be null and we skip (fail-open).
  return [state](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    if (!state->filter_config) {
      ENVOY_LOG_MISC(warn,
                     "Dynamic module filter skipped: remote module was not loaded (fail-open)");
      return;
    }
    const auto& config = state->filter_config;
    const std::string& worker_name = callbacks.dispatcher().name();
    auto pos = worker_name.find_first_of('_');
    ENVOY_BUG(pos != std::string::npos, "worker name is not in expected format worker_{index}");
    uint32_t worker_index;
    if (!absl::SimpleAtoi(worker_name.substr(pos + 1), &worker_index)) {
      IS_ENVOY_BUG("failed to parse worker index from name");
    }
    auto filter =
        std::make_shared<Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilter>(
            config, config->stats_scope_->symbolTable(), worker_index);
    callbacks.addStreamFilter(filter);
    // addStreamFilter() sets decoder/encoder filter callbacks. Initialize the in-module filter
    // after so it can access all necessary context during creation.
    filter->initializeInModuleFilter();
  };
}

Envoy::Http::FilterFactoryCb
DynamicModuleConfigFactory::createFilterFactoryFromProtoWithServerContextTyped(
    const FilterConfig& proto_config, const std::string& stat_prefix,
    Server::Configuration::ServerFactoryContext& context) {
  auto cb_or_error = createFilterFactory(proto_config, stat_prefix, context, context.scope());
  THROW_IF_NOT_OK_REF(cb_or_error.status());
  return cb_or_error.value();
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
DynamicModuleConfigFactory::createRouteSpecificFilterConfigTyped(
    const RouteConfigProto& proto_config, Server::Configuration::ServerFactoryContext&,
    ProtobufMessage::ValidationVisitor&) {

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());
  if (!dynamic_module.ok()) {
    return absl::InvalidArgumentError("Failed to load dynamic module: " +
                                      std::string(dynamic_module.status().message()));
  }

  std::string config;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.filter_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    config = std::move(config_or_error.value());
  }

  absl::StatusOr<Envoy::Extensions::DynamicModules::HttpFilters::
                     DynamicModuleHttpPerRouteFilterConfigConstSharedPtr>
      filter_config =
          Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpPerRouteConfig(
              proto_config.per_route_config_name(), config, std::move(dynamic_module.value()));

  if (!filter_config.ok()) {
    return absl::InvalidArgumentError("Failed to create pre-route filter config: " +
                                      std::string(filter_config.status().message()));
  }
  return filter_config.value();
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
