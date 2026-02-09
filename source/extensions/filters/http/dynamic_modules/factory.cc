#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include "source/common/config/datasource.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/dynamic_modules/module_cache.h"
#include "source/extensions/filters/http/dynamic_modules/filter.h"
#include "source/extensions/filters/http/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

namespace {

absl::StatusOr<
    Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfigSharedPtr>
createFilterConfigFromBytes(absl::string_view module_bytes, absl::string_view sha256_hash,
                            const FilterConfig& proto_config,
                            Server::Configuration::ServerFactoryContext& context,
                            Stats::Scope& scope) {
  const auto& module_config = proto_config.dynamic_module_config();

  auto dynamic_module = Extensions::DynamicModules::newDynamicModuleFromBytes(
      module_bytes, sha256_hash, module_config.do_not_close(), module_config.load_globally());
  if (!dynamic_module.ok()) {
    return absl::InvalidArgumentError("Failed to load dynamic module from bytes: " +
                                      std::string(dynamic_module.status().message()));
  }

  std::string config;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.filter_config());
    if (!config_or_error.ok()) {
      return config_or_error.status();
    }
    config = std::move(config_or_error.value());
  }

  const std::string metrics_namespace =
      module_config.metrics_namespace().empty()
          ? std::string(Extensions::DynamicModules::HttpFilters::DefaultMetricsNamespace)
          : module_config.metrics_namespace();

  return Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
      proto_config.filter_name(), config, metrics_namespace, proto_config.terminal_filter(),
      std::move(dynamic_module.value()), scope, context);
}

Http::FilterFactoryCb createFilterFactoryCallback(
    Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfigSharedPtr
        filter_config) {
  return [config = std::move(filter_config)](Http::FilterChainFactoryCallbacks& callbacks) -> void {
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
    filter->initializeInModuleFilter();
    callbacks.addStreamFilter(filter);
  };
}

} // namespace

absl::StatusOr<Http::FilterFactoryCb> DynamicModuleConfigFactory::createFilterFactory(
    const FilterConfig& proto_config, const std::string&,
    Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope,
    Init::Manager* init_manager) {

  const auto& module_config = proto_config.dynamic_module_config();

  if (module_config.has_module()) {
    return createFilterFactoryFromAsyncDataSource(proto_config, context, scope, init_manager);
  }

  // Legacy path: load module by name.
  if (module_config.name().empty()) {
    return absl::InvalidArgumentError(
        "Either 'name' or 'module' must be specified in dynamic_module_config");
  }

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

  // When the runtime guard is enabled, register the metrics namespace as a custom stat namespace.
  // This causes the namespace prefix to be stripped from prometheus output and no envoy_ prefix
  // is added. This is the legacy behavior for backward compatibility.
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
    context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
  }

  return createFilterFactoryCallback(filter_config.value());
}

// Handles the AsyncDataSource-based module loading path (local files, inline bytes, and remote
// HTTP). For remote sources, modules are cached by SHA256 hash with two fetch modes:
//   - NACK mode: reject the config immediately, fetch in the background, succeed on retry.
//   - Warming mode: block server init until the fetch completes (or fails).
absl::StatusOr<Http::FilterFactoryCb>
DynamicModuleConfigFactory::createFilterFactoryFromAsyncDataSource(
    const FilterConfig& proto_config, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope& scope, Init::Manager* init_manager) {

  const auto& module_config = proto_config.dynamic_module_config();
  const auto& async_source = module_config.module();

  const std::string metrics_namespace =
      module_config.metrics_namespace().empty()
          ? std::string(Extensions::DynamicModules::HttpFilters::DefaultMetricsNamespace)
          : module_config.metrics_namespace();

  if (async_source.has_local()) {
    auto data_or_error = Config::DataSource::read(async_source.local(), true, context.api());
    if (!data_or_error.ok()) {
      return absl::InvalidArgumentError("Failed to read module data: " +
                                        std::string(data_or_error.status().message()));
    }

    const std::string& module_bytes = data_or_error.value();
    if (module_bytes.empty()) {
      return absl::InvalidArgumentError("Module data is empty");
    }

    auto filter_config =
        createFilterConfigFromBytes(module_bytes, "", proto_config, context, scope);
    if (!filter_config.ok()) {
      return filter_config.status();
    }

    context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
    return createFilterFactoryCallback(filter_config.value());
  }

  if (async_source.has_remote()) {
    const auto& remote_source = async_source.remote();
    const std::string& sha256_hash = remote_source.sha256();

    if (sha256_hash.empty()) {
      return absl::InvalidArgumentError("SHA256 hash is required for remote module sources");
    }

    auto& module_cache = Extensions::DynamicModules::getModuleCache();
    auto now = context.mainThreadDispatcher().timeSource().monotonicTime();
    auto cache_result = module_cache.lookup(sha256_hash, now);

    if (cache_result.cache_hit && cache_result.module) {
      auto filter_config = createFilterConfigFromBytes(*cache_result.module, sha256_hash,
                                                       proto_config, context, scope);
      if (!filter_config.ok()) {
        return filter_config.status();
      }

      context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
      return createFilterFactoryCallback(filter_config.value());
    }

    if (cache_result.fetch_in_progress) {
      if (module_config.nack_on_module_cache_miss()) {
        return absl::UnavailableError("Module fetch in progress, NACK'ing configuration");
      }
      // TODO(kanurag94): support waiting on in-progress fetches in warming mode.
      return absl::UnavailableError("Module fetch in progress");
    }

    if (cache_result.cache_hit && !cache_result.module) {
      // Negative cache hit -- a recent fetch failed. In NACK mode, reject immediately.
      // In warming mode, fall through to re-fetch since the module may now be available.
      if (module_config.nack_on_module_cache_miss()) {
        return absl::UnavailableError(
            "Module fetch recently failed (negative cache hit), NACK'ing configuration");
      }
    }

    // NACK mode: kick off a background fetch, then NACK this config update. The control
    // plane will re-push the config, and the next attempt will find the module in cache.
    if (module_config.nack_on_module_cache_miss()) {
      module_cache.markInProgress(sha256_hash, now);

      // Use shared_ptr<unique_ptr<DeferredDeletable>> to keep the adapter+fetcher alive until
      // the fetch callback fires. The shared_ptr is captured by the callback closure, forming
      // a reference cycle that keeps everything alive. The cycle is broken inside the callback
      // via holder->release() + deferredDelete. Without this, calling deferredDelete immediately
      // after fetch() would destroy the fetcher at the end of the current event loop iteration,
      // canceling the in-flight HTTP request before the response arrives.
      auto holder = std::make_shared<std::unique_ptr<Event::DeferredDeletable>>();

      auto adapter = std::make_unique<Extensions::DynamicModules::RemoteDataFetcherAdapter>(
          [sha256_hash, &context, holder](const std::string& data) {
            auto& cache = Extensions::DynamicModules::getModuleCache();
            auto fetch_time = context.mainThreadDispatcher().timeSource().monotonicTime();
            // RemoteDataFetcher already verifies the SHA256 hash before calling onSuccess,
            // so non-empty data here is guaranteed to be valid.
            cache.update(sha256_hash, data, fetch_time);
            // Break the reference cycle and schedule cleanup.
            if (*holder) {
              context.mainThreadDispatcher().deferredDelete(
                  Event::DeferredDeletablePtr{holder->release()});
            }
          });

      auto fetcher = std::make_unique<Config::DataFetcher::RemoteDataFetcher>(
          context.clusterManager(), remote_source.http_uri(), sha256_hash, *adapter);
      auto fetcher_ptr = fetcher.get();
      adapter->setFetcher(std::move(fetcher));
      *holder = std::move(adapter);
      fetcher_ptr->fetch();

      return absl::UnavailableError(
          "Remote module not in cache, background fetch started, NACK'ing configuration");
    }

    // Warming mode: block server init until the fetch completes. The init manager will
    // not transition to Initialized until the RemoteAsyncDataProvider signals ready().
    if (init_manager == nullptr) {
      return absl::InvalidArgumentError(
          "Init manager required for warming mode with remote module sources");
    }

    module_cache.markInProgress(sha256_hash, now);

    // AsyncLoadState is shared between the fetch callback (which populates filter_config)
    // and the returned factory callback (which reads it). Also prevents the
    // RemoteAsyncDataProvider from being destroyed before the fetch completes.
    struct AsyncLoadState {
      Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfigSharedPtr filter_config;
      RemoteAsyncDataProviderPtr remote_provider;
    };
    auto state = std::make_shared<AsyncLoadState>();

    // SHA256 verification is handled by the underlying RemoteDataFetcher.
    // Capture a weak_ptr to break the reference cycle: state owns remote_provider,
    // and remote_provider's callback would otherwise prevent state from being freed.
    std::weak_ptr<AsyncLoadState> weak_state = state;
    state->remote_provider = std::make_unique<RemoteAsyncDataProvider>(
        context.clusterManager(), *init_manager, remote_source, context.mainThreadDispatcher(),
        context.api().randomGenerator(), false,
        [weak_state, sha256_hash, proto_config_copy = proto_config, &context, &scope,
         metrics_namespace](const std::string& data) {
          auto& cache = Extensions::DynamicModules::getModuleCache();
          auto fetch_time = context.mainThreadDispatcher().timeSource().monotonicTime();
          cache.update(sha256_hash, data, fetch_time);

          auto state = weak_state.lock();
          if (data.empty()) {
            ENVOY_LOG_MISC(warn, "Remote dynamic module fetch failed for SHA256 {}", sha256_hash);
            return;
          }
          if (!state) {
            return;
          }
          auto filter_config =
              createFilterConfigFromBytes(data, sha256_hash, proto_config_copy, context, scope);
          if (!filter_config.ok()) {
            ENVOY_LOG_MISC(warn,
                           "Remote dynamic module fetched but failed to load for SHA256 {}: {}",
                           sha256_hash, filter_config.status().message());
            return;
          }
          state->filter_config = filter_config.value();
          context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
        });

    // If the fetch failed, filter_config will be null and we silently skip (fail-open).
    return [state](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      if (!state->filter_config) {
        return;
      }
      createFilterFactoryCallback(state->filter_config)(callbacks);
    };
  }

  return absl::InvalidArgumentError("Invalid AsyncDataSource: neither local nor remote specified");
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
