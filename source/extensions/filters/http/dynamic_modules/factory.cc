#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include <filesystem>

#include "source/common/runtime/runtime_features.h"
#include "source/extensions/common/wasm/remote_async_datasource.h"
#include "source/extensions/dynamic_modules/background_fetch_manager.h"
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
    const envoy::extensions::dynamic_modules::v3::DynamicModuleConfig& module_config,
    Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope) {

  std::string config;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.filter_config());
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
              std::move(dynamic_module), scope, context);

  if (!filter_config.ok()) {
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
    Init::Manager* init_manager) {

  const auto& module_config = proto_config.dynamic_module_config();

  // Load the module: local file, remote HTTP source, or by name.
  absl::StatusOr<Extensions::DynamicModules::DynamicModulePtr> dynamic_module;
  if (module_config.has_module()) {
    if (module_config.module().has_remote()) {
      const auto& sha256 = module_config.module().remote().sha256();

      // Check if a previously fetched module with the same SHA256 already exists on disk.
      // newDynamicModuleFromBytes writes to a deterministic path based on SHA256, so the
      // filesystem itself acts as the cache.
      auto cached_path = Extensions::DynamicModules::moduleTempPath(sha256);
      if (std::filesystem::exists(cached_path)) {
        dynamic_module = Extensions::DynamicModules::newDynamicModule(
            cached_path, module_config.do_not_close(), module_config.load_globally());
        if (dynamic_module.ok()) {
          Extensions::DynamicModules::BackgroundFetchManager::singleton(context.singletonManager())
              ->erase(sha256);
          return buildFilterFactoryCallback(std::move(dynamic_module.value()), proto_config,
                                            module_config, context, scope);
        }
        // File exists but failed to load — re-fetching the same SHA256 would produce
        // identical bytes, so there is no point in falling through to the remote path.
        return absl::InvalidArgumentError("Cached remote module failed to load: " +
                                          std::string(dynamic_module.status().message()));
      }

      // In NACK mode, reject the config and kick off a background fetch. The control
      // plane will retry, and the next attempt picks up the cached file above.
      if (module_config.nack_on_cache_miss()) {
        Extensions::DynamicModules::BackgroundFetchManager::singleton(context.singletonManager())
            ->fetchIfNeeded(sha256, context.clusterManager(), module_config.module().remote());
        return absl::InvalidArgumentError(
            "Remote module not cached; background fetch in progress. SHA256: " + sha256);
      }

      // No cached file — need async fetch, which requires init_manager.
      if (init_manager == nullptr) {
        return absl::InvalidArgumentError("Remote module sources require an init manager");
      }
      return createFilterFactoryFromRemoteSource(proto_config, module_config, context, scope,
                                                 *init_manager);
    }
    if (!module_config.module().has_local() || !module_config.module().local().has_filename()) {
      return absl::InvalidArgumentError(
          "Only local file path or remote HTTP source is supported for module sources");
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

  return buildFilterFactoryCallback(std::move(dynamic_module.value()), proto_config, module_config,
                                    context, scope);
}

absl::StatusOr<Http::FilterFactoryCb>
DynamicModuleConfigFactory::createFilterFactoryFromRemoteSource(
    const FilterConfig& proto_config,
    const envoy::extensions::dynamic_modules::v3::DynamicModuleConfig& module_config,
    Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope,
    Init::Manager& init_manager) {

  // Shared state: the filter factory callback is populated asynchronously after the remote fetch
  // completes, then used by per-request lambda below. The RemoteAsyncDataProvider is stored here
  // to keep it alive for the duration of the fetch (including retries).
  struct AsyncState {
    Http::FilterFactoryCb filter_factory_cb;
    RemoteAsyncDataProviderPtr remote_provider;
  };
  auto async_state = std::make_shared<AsyncState>();

  // Copies for use in the callback — the originals may not outlive the async fetch.
  const FilterConfig proto_config_copy = proto_config;
  const auto module_config_copy = module_config;

  // Use a weak_ptr in the callback to break the reference cycle:
  // async_state -> remote_provider -> callback -> async_state.
  std::weak_ptr<AsyncState> weak_state = async_state;

  async_state->remote_provider = std::make_unique<RemoteAsyncDataProvider>(
      context.clusterManager(), init_manager, module_config.module().remote(),
      context.mainThreadDispatcher(), context.api().randomGenerator(),
      /*allow_empty=*/true,
      [weak_state, proto_config_copy, module_config_copy, &context,
       &scope](const std::string& data) {
        auto state = weak_state.lock();
        if (!state) {
          return;
        }
        if (data.empty()) {
          ENVOY_LOG_TO_LOGGER(
              Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), error,
              "Remote dynamic module fetch returned empty data; filter will not be installed");
          return;
        }
        auto module_or_error = Extensions::DynamicModules::newDynamicModuleFromBytes(
            data, module_config_copy.module().remote().sha256(), module_config_copy.do_not_close(),
            module_config_copy.load_globally());
        if (!module_or_error.ok()) {
          ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules),
                              error, "Failed to load remote dynamic module from bytes: {}",
                              module_or_error.status().message());
          return;
        }

        auto cb_or_error =
            buildFilterFactoryCallback(std::move(module_or_error.value()), proto_config_copy,
                                       module_config_copy, context, scope);
        if (!cb_or_error.ok()) {
          ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules),
                              error, "Failed to create filter config from remote module: {}",
                              cb_or_error.status().message());
          return;
        }
        state->filter_factory_cb = cb_or_error.value();
      });

  // Note: if the remote fetch fails (network error, bad data, etc.), filter_factory_cb remains
  // empty and this lambda becomes a no-op — the filter is not installed and requests pass through.
  // This is fail-open, consistent with how Wasm remote data providers handle fetch failures.
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

  absl::string_view filter_name = proto_config.filter_name();
  if (filter_name.empty()) {
    filter_name = proto_config.per_route_config_name();
  }

  absl::StatusOr<Envoy::Extensions::DynamicModules::HttpFilters::
                     DynamicModuleHttpPerRouteFilterConfigConstSharedPtr>
      filter_config =
          Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpPerRouteConfig(
              filter_name, config, std::move(dynamic_module.value()));

  if (!filter_config.ok()) {
    return absl::InvalidArgumentError("Failed to create pre-route filter config: " +
                                      std::string(filter_config.status().message()));
  }
  return filter_config.value();
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
