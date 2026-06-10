#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include "source/common/common/logger.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/filters/http/dynamic_modules/filter.h"
#include "source/extensions/filters/http/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

namespace {

// Builds a FilterFactoryCb from an already-loaded DynamicModule.
// Extracted because both the synchronous path and the remote fetch callback need it.
absl::StatusOr<Http::FilterFactoryCb> buildFilterFactoryCallback(
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, const FilterConfig& proto_config,
    Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope) {

  std::string config;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.filter_config());
    if (!config_or_error.ok()) {
      Extensions::DynamicModules::incrementLoadFailure(
          context, proto_config.filter_name(), Extensions::DynamicModules::ConfigInitErrorStat);
      return config_or_error.status();
    }
    config = std::move(config_or_error.value());
  }

  // Use configured metrics namespace or fall back to the default.
  const std::string metrics_namespace =
      proto_config.dynamic_module_config().metrics_namespace().empty()
          ? std::string(Extensions::DynamicModules::HttpFilters::DefaultMetricsNamespace)
          : proto_config.dynamic_module_config().metrics_namespace();

  absl::StatusOr<
      Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfigSharedPtr>
      filter_config =
          Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
              proto_config.filter_name(), config, metrics_namespace, proto_config.terminal_filter(),
              std::move(dynamic_module), scope, context);

  if (!filter_config.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        context, proto_config.filter_name(), Extensions::DynamicModules::ConfigInitErrorStat);
    return absl::InvalidArgumentError("Failed to create filter config: " +
                                      std::string(filter_config.status().message()));
  }

  // When the runtime guard is enabled, register the metrics namespace as a custom stat namespace.
  // This causes the namespace prefix to be stripped from prometheus output and no envoy_ prefix
  // is added. This is the legacy behavior for backward compatibility.
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

    // The addStreamFilter() will call the setDecoderFilterCallbacks first then
    // setEncoderFilterCallbacks.
    // We can initialize the in-module filter after we have both callbacks to ensure the in module
    // filter can access all the necessary information during creation.
    filter->initializeInModuleFilter();
  };
}

} // namespace

absl::StatusOr<Http::FilterFactoryCb> DynamicModuleConfigFactory::createFilterFactory(
    const FilterConfig& proto_config, const std::string&,
    Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope,
    OptRef<Init::Manager> init_manager) {

  const auto& module_config = proto_config.dynamic_module_config();

  // Shared state for the asynchronous remote-fetch path: the filter factory callback is populated
  // after the fetch completes and then used by the per-request lambda below. The loading_state
  // (which owns the RemoteAsyncDataProvider) is held here to keep the fetch alive for its duration,
  // including retries.
  struct AsyncState {
    Http::FilterFactoryCb filter_factory_cb;
    Extensions::DynamicModules::AsyncLoadingStateSharedPtr loading_state;
  };
  auto async_state = std::make_shared<AsyncState>();

  // Use a weak_ptr in the callback to break the reference cycle:
  // async_state -> loading_state -> on_loaded -> async_state.
  std::weak_ptr<AsyncState> weak_state = async_state;

  // Invoked on the main thread once an asynchronously fetched module finishes loading.
  auto on_loaded = [weak_state, proto_config, &context,
                    &scope](Extensions::DynamicModules::DynamicModulePtr dynamic_module) {
    auto state = weak_state.lock();
    if (!state) {
      return;
    }
    auto cb_or_error =
        buildFilterFactoryCallback(std::move(dynamic_module), proto_config, context, scope);
    if (!cb_or_error.ok()) {
      ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules),
                          error, "Failed to create filter config from remote module: {}",
                          cb_or_error.status().message());
      return;
    }
    state->filter_factory_cb = cb_or_error.value();
  };

  // The shared loader emits the load-level failure counters (module_load_error /
  // remote_fetch_error) itself, tagged with the filter name; on a non-ok status we just propagate
  // it without double-counting.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      module_config, proto_config.filter_name(), context, init_manager, std::move(on_loaded));
  RETURN_IF_NOT_OK_REF(load_result.status());

  // Synchronous load (local file, by name, or remote cache hit): build the factory now.
  if (load_result->loaded != nullptr) {
    return buildFilterFactoryCallback(std::move(load_result->loaded), proto_config, context, scope);
  }

  ASSERT(load_result->async != nullptr, "Async loading state must be populated for async loads");

  // Asynchronous remote fetch in progress: keep the loading state alive and return a fail-open
  // factory that becomes active once the fetch completes. If the fetch fails (network error, bad
  // data, etc.), filter_factory_cb remains empty and this lambda is a no-op — the filter is not
  // installed and requests pass through, consistent with how Wasm remote data providers behave.
  async_state->loading_state = std::move(load_result->async);
  return [async_state](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    if (async_state->filter_factory_cb) {
      async_state->filter_factory_cb(callbacks);
    }
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
    const RouteConfigProto& proto_config, Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor&) {

  const auto& module_config = proto_config.dynamic_module_config();

  absl::string_view filter_name = proto_config.filter_name();
  if (filter_name.empty()) {
    filter_name = proto_config.per_route_config_name();
  }

  // Per-route configs report every failure under the dedicated ``per_route_config_error`` stat, so
  // no factory context is passed to the shared loader (which would otherwise emit its own
  // ``module_load_error``/``remote_fetch_error`` counters and double-count). As a result remote
  // module sources are not supported on the per-route path; only local-file and by-name sources
  // load synchronously.
  auto load_result =
      Extensions::DynamicModules::newDynamicModuleByConfig(module_config, filter_name);
  if (!load_result.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        context, filter_name, Extensions::DynamicModules::PerRouteConfigErrorStat);
    return load_result.status();
  }
  auto dynamic_module = std::move(load_result->loaded);

  std::string config;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.filter_config());
    if (!config_or_error.ok()) {
      Extensions::DynamicModules::incrementLoadFailure(
          context, filter_name, Extensions::DynamicModules::PerRouteConfigErrorStat);
      return config_or_error.status();
    }
    config = std::move(config_or_error.value());
  }

  absl::StatusOr<Envoy::Extensions::DynamicModules::HttpFilters::
                     DynamicModuleHttpPerRouteFilterConfigConstSharedPtr>
      filter_config =
          Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpPerRouteConfig(
              filter_name, config, std::move(dynamic_module));

  if (!filter_config.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        context, filter_name, Extensions::DynamicModules::PerRouteConfigErrorStat);
    return absl::InvalidArgumentError("Failed to create pre-route filter config: " +
                                      std::string(filter_config.status().message()));
  }
  return filter_config.value();
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
