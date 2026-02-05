#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/hex.h"
#include "source/common/config/datasource.h"
#include "source/common/crypto/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/dynamic_modules/code_cache.h"
#include "source/extensions/filters/http/dynamic_modules/filter.h"
#include "source/extensions/filters/http/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

namespace {

// Helper function to load a module from bytes and create the filter config.
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

// Helper to create the filter factory callback from a filter config.
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

  // Check if the new 'module' field is set.
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

absl::StatusOr<Http::FilterFactoryCb>
DynamicModuleConfigFactory::createFilterFactoryFromAsyncDataSource(
    const FilterConfig& proto_config, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope& scope, Init::Manager* init_manager) {

  const auto& module_config = proto_config.dynamic_module_config();
  const auto& async_source = module_config.module();

  // Use configured metrics namespace or fall back to the default.
  const std::string metrics_namespace =
      module_config.metrics_namespace().empty()
          ? std::string(Extensions::DynamicModules::HttpFilters::DefaultMetricsNamespace)
          : module_config.metrics_namespace();

  if (async_source.has_local()) {
    // Synchronous path: local file or inline bytes.
    auto data_or_error = Config::DataSource::read(async_source.local(), true, context.api());
    if (!data_or_error.ok()) {
      return absl::InvalidArgumentError("Failed to read module data: " +
                                        std::string(data_or_error.status().message()));
    }

    const std::string& module_bytes = data_or_error.value();
    if (module_bytes.empty()) {
      return absl::InvalidArgumentError("Module data is empty");
    }

    // Compute SHA256 for the local data (no verification needed, just for temp file naming).
    auto filter_config =
        createFilterConfigFromBytes(module_bytes, "", proto_config, context, scope);
    if (!filter_config.ok()) {
      return filter_config.status();
    }

    context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
    return createFilterFactoryCallback(filter_config.value());
  }

  if (async_source.has_remote()) {
    // Asynchronous path: remote HTTP fetch.
    const auto& remote_source = async_source.remote();
    const std::string& sha256_hash = remote_source.sha256();

    if (sha256_hash.empty()) {
      return absl::InvalidArgumentError("SHA256 hash is required for remote module sources");
    }

    // Check the code cache first.
    auto& code_cache = Extensions::DynamicModules::getCodeCache();
    auto now = context.mainThreadDispatcher().timeSource().monotonicTime();
    auto cache_result = code_cache.lookup(sha256_hash, now);

    if (cache_result.cache_hit && !cache_result.code.empty()) {
      // Cache hit with valid code - load synchronously.
      auto filter_config =
          createFilterConfigFromBytes(cache_result.code, sha256_hash, proto_config, context, scope);
      if (!filter_config.ok()) {
        return filter_config.status();
      }

      context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
      return createFilterFactoryCallback(filter_config.value());
    }

    if (cache_result.cache_hit && cache_result.code.empty()) {
      // Negative cache hit (recent fetch failure).
      if (module_config.nack_on_module_cache_miss()) {
        return absl::UnavailableError(
            "Module fetch recently failed (negative cache hit), NACK'ing configuration");
      }
      // For warming mode with negative cache, we still need to try fetching again.
    }

    if (cache_result.fetch_in_progress) {
      // Another fetch is already in progress.
      if (module_config.nack_on_module_cache_miss()) {
        return absl::UnavailableError("Module fetch in progress, NACK'ing configuration");
      }
      // For warming mode, we'd need to wait - but this is complex to implement.
      // For now, treat as unavailable.
      return absl::UnavailableError("Module fetch in progress");
    }

    // Need to fetch the module.
    if (module_config.nack_on_module_cache_miss()) {
      // NACK mode: Start background fetch and reject this config.
      code_cache.markInProgress(sha256_hash, now);

      // Create a holder structure that keeps both adapter and fetcher alive together.
      struct FetchHolder : public Event::DeferredDeletable {
        Extensions::DynamicModules::RemoteDataFetcherAdapter adapter;
        std::unique_ptr<Config::DataFetcher::RemoteDataFetcher> fetcher;

        FetchHolder(Extensions::DynamicModules::FetchCallback cb) : adapter(std::move(cb)) {}
        ~FetchHolder() override = default;
      };

      auto holder = std::make_unique<FetchHolder>([sha256_hash, &context](const std::string& data) {
        auto& cache = Extensions::DynamicModules::getCodeCache();
        auto fetch_time = context.mainThreadDispatcher().timeSource().monotonicTime();

        if (!data.empty()) {
          // Verify SHA256.
          Buffer::OwnedImpl buffer(data);
          auto& crypto_util = Common::Crypto::UtilitySingleton::get();
          const std::string computed_hash = Hex::encode(crypto_util.getSha256Digest(buffer));
          if (computed_hash == sha256_hash) {
            cache.update(sha256_hash, data, fetch_time);
            return;
          }
          // Hash mismatch - treat as failure.
          ENVOY_LOG_MISC(warn, "Dynamic module SHA256 mismatch: expected {}, got {}", sha256_hash,
                         computed_hash);
        }
        // Fetch failed or hash mismatch - update with empty data for negative caching.
        cache.update(sha256_hash, "", fetch_time);
      });

      holder->fetcher = std::make_unique<Config::DataFetcher::RemoteDataFetcher>(
          context.clusterManager(), remote_source.http_uri(), sha256_hash, holder->adapter);
      holder->fetcher->fetch();

      // Defer deletion of the holder to ensure it lives until the callback is invoked.
      context.mainThreadDispatcher().deferredDelete(std::move(holder));

      return absl::UnavailableError(
          "Remote module not in cache, background fetch started, NACK'ing configuration");
    }

    // Warming mode: Use init manager to block until fetch completes.
    if (init_manager == nullptr) {
      return absl::InvalidArgumentError(
          "Init manager required for warming mode with remote module sources");
    }

    // Mark as in progress.
    code_cache.markInProgress(sha256_hash, now);

    // Create a shared state to hold the fetched data, filter config, and keep the provider alive.
    struct AsyncLoadState {
      std::string module_bytes;
      Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfigSharedPtr filter_config;
      RemoteAsyncDataProviderPtr remote_provider;
      bool fetch_completed{false};
      bool fetch_success{false};
    };
    auto state = std::make_shared<AsyncLoadState>();

    // Use RemoteAsyncDataProvider for the fetch.
    // The callback will be invoked when the fetch completes.
    state->remote_provider = std::make_unique<RemoteAsyncDataProvider>(
        context.clusterManager(), *init_manager, remote_source, context.mainThreadDispatcher(),
        context.api().randomGenerator(), false,
        [state, sha256_hash, proto_config_copy = proto_config, &context, &scope,
         metrics_namespace](const std::string& data) {
          auto& cache = Extensions::DynamicModules::getCodeCache();
          auto fetch_time = context.mainThreadDispatcher().timeSource().monotonicTime();

          state->fetch_completed = true;
          if (!data.empty()) {
            // Verify SHA256.
            Buffer::OwnedImpl buffer(data);
            auto& crypto_util = Common::Crypto::UtilitySingleton::get();
            const std::string computed_hash = Hex::encode(crypto_util.getSha256Digest(buffer));
            if (computed_hash == sha256_hash) {
              state->module_bytes = data;
              state->fetch_success = true;
              cache.update(sha256_hash, data, fetch_time);

              // Now create the filter config.
              auto filter_config =
                  createFilterConfigFromBytes(data, sha256_hash, proto_config_copy, context, scope);
              if (filter_config.ok()) {
                state->filter_config = filter_config.value();
                context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
              }
              return;
            }
            ENVOY_LOG_MISC(warn, "Dynamic module SHA256 mismatch: expected {}, got {}", sha256_hash,
                           computed_hash);
          }
          cache.update(sha256_hash, "", fetch_time);
        });

    // Return a factory callback that uses the async-loaded config.
    return [state](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      if (!state->filter_config) {
        // Module failed to load - skip adding filter (fail open behavior for warming mode).
        return;
      }

      const std::string& worker_name = callbacks.dispatcher().name();
      auto pos = worker_name.find_first_of('_');
      ENVOY_BUG(pos != std::string::npos, "worker name is not in expected format worker_{index}");
      uint32_t worker_index;
      if (!absl::SimpleAtoi(worker_name.substr(pos + 1), &worker_index)) {
        IS_ENVOY_BUG("failed to parse worker index from name");
      }
      auto filter =
          std::make_shared<Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilter>(
              state->filter_config, state->filter_config->stats_scope_->symbolTable(),
              worker_index);
      filter->initializeInModuleFilter();
      callbacks.addStreamFilter(filter);
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
