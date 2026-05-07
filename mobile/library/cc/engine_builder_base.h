#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/debugging/leak_check.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "library/cc/engine.h"
#include "library/common/engine_types.h"
#include "library/common/internal_engine.h"
#include "source/common/common/base_logger.h"
#include "source/server/options_impl_base.h"
#include "source/common/runtime/runtime_features.h"
#include "envoy/type/matcher/v3/string.pb.h"

namespace Envoy {
namespace Platform {

/**
 * Base class template implementing the Curiously Recurring Template Pattern (CRTP)
 * to share common bootstrap generation and lifecycle logic between Engine Builders.
 *
 * How to Subclass:
 * Subclasses should derive from this base class passing themselves as the template parameter:
 *   class MobileEngineBuilder : public Platform::EngineBuilderBase<MobileEngineBuilder> { ... };
 *
 * Key CRTP Hooks the Derived Class SHOULD/MUST implement:
 * 1. Pre-run and Post-run Lifecycle Hooks:
 *    - `void preRunSetup(InternalEngine* engine)`: Called inside build() on the engine thread
 *      prior to starting the event loop. Derived builders should register dynamic key-value stores
 *      or string accessors here.
 *    - `void postRunSetup(Engine* engine)`: Called inside build() immediately after starting
 *      the engine. Used to initialize platform monitors (e.g., NetworkChangeMonitor).
 *
 * 2. Bootstrap Configuration Hooks:
 *    - `absl::Status configureNode(envoy::config::core::v3::Node* node)`:
 *      Configures the Bootstrap's node metadata (e.g., setting app_id, app_version, device_os).
 *    - `absl::Status configureRouteConfig(envoy::config::route::v3::RouteConfiguration*
 * route_config)`: Configures virtual hosts, domains, routes, and early data policies.
 *    - `absl::Status configureStaticClusters(
 *          Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster>* clusters)`:
 *      Defines and registers static upstream clusters (e.g., base DFP cluster or mock test
 * clusters).
 *    - `absl::Status configXds(envoy::config::bootstrap::v3::Bootstrap* bootstrap)`:
 *      Configures dynamic xDS RTDS/CDS layers on the bootstrap.
 *
 * 3. Optional HTTP Filter Config Overrides:
 *    - `absl::Status configureHttpFilters(
 *          std::function<envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter*()>
 * add_filter)`: Customizes the HttpFilter chain (e.g., AlternateProtocolsCache, Decompressors,
 * SocketTagging).
 *    - `void configureCustomRouterFilter(
 *          envoy::extensions::filters::http::router::v3::Router& router_config)`:
 *      Applies custom tweaks to the terminal HTTP router filter.
 *    - `absl::Status configureTracing(...)`:
 *      Configures custom tracing providers.
 */
template <typename T> class EngineBuilderBase {
public:
  EngineBuilderBase() : callbacks_(std::make_unique<::Envoy::EngineCallbacks>()) {}
  virtual ~EngineBuilderBase() = default;
  EngineBuilderBase(EngineBuilderBase&&) = default;

  T& setLogLevel(Logger::Logger::Levels log_level) {
    log_level_ = log_level;
    return static_cast<T&>(*this);
  }

  T& setLogger(std::unique_ptr<EnvoyLogger> logger) {
    logger_ = std::move(logger);
    return static_cast<T&>(*this);
  }

  T& enableLogger(bool logger_on) {
    enable_logger_ = logger_on;
    return static_cast<T&>(*this);
  }

  T& setEngineCallbacks(std::unique_ptr<EngineCallbacks> callbacks) {
    callbacks_ = std::move(callbacks);
    return static_cast<T&>(*this);
  }

  T& setOnEngineRunning(absl::AnyInvocable<void()> closure) {
    if (!callbacks_) {
      callbacks_ = std::make_unique<EngineCallbacks>();
    }
    callbacks_->on_engine_running_ = std::move(closure);
    return static_cast<T&>(*this);
  }

  T& setOnEngineExit(absl::AnyInvocable<void()> closure) {
    if (!callbacks_) {
      callbacks_ = std::make_unique<EngineCallbacks>();
    }
    callbacks_->on_exit_ = std::move(closure);
    return static_cast<T&>(*this);
  }

  T& setEventTracker(std::unique_ptr<EnvoyEventTracker> event_tracker) {
    event_tracker_ = std::move(event_tracker);
    return static_cast<T&>(*this);
  }

  T& setBufferHighWatermark(size_t high_watermark) {
    high_watermark_ = high_watermark;
    return static_cast<T&>(*this);
  }

  T& enableStatsCollection(bool stats_collection_on) {
    enable_stats_collection_ = stats_collection_on;
    return static_cast<T&>(*this);
  }

  T& addReloadableRuntimeGuard(std::string guard, bool value) {
    reloadable_runtime_guards_.emplace_back(std::move(guard), value);
    return static_cast<T&>(*this);
  }

  T& addRestartRuntimeGuard(std::string guard, bool value) {
    restart_runtime_guards_.emplace_back(std::move(guard), value);
    return static_cast<T&>(*this);
  }

  T& setStreamIdleTimeoutSeconds(int stream_idle_timeout_seconds) {
    stream_idle_timeout_seconds_ = stream_idle_timeout_seconds;
    return static_cast<T&>(*this);
  }

  T& setPerConnectionBufferLimitBytes(uint32_t per_connection_buffer_limit_bytes) {
    per_connection_buffer_limit_bytes_ = per_connection_buffer_limit_bytes;
    return static_cast<T&>(*this);
  }

  // If true, all HTTP requests are handled on a dedicated worker thread instead of on the Envoy
  // main thread which also handles all xDS requests.
  // Note: Engine in worker thread model doesn't support platform certificate validation and system
  // proxy settings. And these settings will be ignored if worker thread model is enabled.
  T& enableWorkerThread(bool use_worker_thread) {
    use_worker_thread_ = use_worker_thread;
    return static_cast<T&>(*this);
  }

  absl::StatusOr<std::shared_ptr<Engine>> build() {
    auto bootstrap = static_cast<T*>(this)->generateBootstrap();
    if (!bootstrap.ok()) {
      return bootstrap.status();
    }

    auto envoy_engine = std::make_unique<::Envoy::InternalEngine>(
        std::move(callbacks_), std::move(logger_), std::move(event_tracker_),
        /*thread_priority=*/absl::nullopt, high_watermark_, enable_logger_, useWorkerThread());

    // Pre-run setup (to be implemented by derived classes if needed)
    static_cast<T*>(this)->preRunSetup(envoy_engine.get());

    auto options = std::make_shared<Envoy::OptionsImplBase>();
    options->setConfigProto(std::move(bootstrap.value()));
    options->setLogLevel(static_cast<spdlog::level::level_enum>(log_level_));
    options->setConcurrency(1);

    envoy_engine->run(options);

    auto engine_wrapper =
        std::shared_ptr<Engine>(new Engine(absl::IgnoreLeak(envoy_engine.release())));

    static_cast<T*>(this)->postRunSetup(engine_wrapper.get());

    return engine_wrapper;
  }

  absl::StatusOr<std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap>> generateBootstrap() {
    auto bootstrap = std::make_unique<envoy::config::bootstrap::v3::Bootstrap>();

    // Set up the HCM, make sure a route config is provided.
    ::envoy::extensions::filters::network::http_connection_manager::v3::
        EnvoyMobileHttpConnectionManager api_listener_config;
    auto* hcm = api_listener_config.mutable_config();
    hcm->set_stat_prefix("hcm");
    hcm->set_server_header_transformation(
        ::envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
            PASS_THROUGH);
    ::envoy::extensions::filters::network::http_connection_manager::v3::
        HttpConnectionManager_Tracing tracing;
    if (auto status = static_cast<T*>(this)->configureTracing(&tracing); !status.ok()) {
      return status;
    }
    if (tracing.ByteSizeLong() > 0) {
      *hcm->mutable_tracing() = std::move(tracing);
    }
    hcm->mutable_stream_idle_timeout()->set_seconds(stream_idle_timeout_seconds_);
    if (auto status = static_cast<T*>(this)->configureRouteConfig(hcm->mutable_route_config());
        !status.ok()) {
      return status;
    }

    if (auto status = static_cast<T*>(this)->configureHttpFilters(
            [hcm]() { return hcm->add_http_filters(); });
        !status.ok()) {
      return status;
    }

    // Add the router filter to the HCM last.
    auto* router_filter = hcm->add_http_filters();
    router_filter->set_name("envoy.router");
    ::envoy::extensions::filters::http::router::v3::Router router_config;
    static_cast<T*>(this)->configureCustomRouterFilter(router_config);
    router_filter->mutable_typed_config()->PackFrom(router_config);

    auto* static_resources = bootstrap->mutable_static_resources();

    // Finally create the base API listener, and point it at the HCM.
    auto* base_listener = static_resources->add_listeners();
    base_listener->set_name("base_api_listener");
    auto* base_address = base_listener->mutable_address();
    base_address->mutable_socket_address()->set_protocol(
        ::envoy::config::core::v3::SocketAddress::TCP);
    base_address->mutable_socket_address()->set_address("0.0.0.0");
    base_address->mutable_socket_address()->set_port_value(10000);
    base_listener->mutable_per_connection_buffer_limit_bytes()->set_value(
        per_connection_buffer_limit_bytes_);
    base_listener->mutable_api_listener()->mutable_api_listener()->PackFrom(api_listener_config);

    // Add all clusters to the bootstrap, there should be at least one.
    if (auto status =
            static_cast<T*>(this)->configureStaticClusters(static_resources->mutable_clusters());
        !status.ok()) {
      return status;
    }

    if (watchdog_.has_value()) {
      *bootstrap->mutable_watchdogs() = std::move(watchdog_).value();
    }

    if (auto status = static_cast<T*>(this)->configureNode(bootstrap->mutable_node());
        !status.ok()) {
      return status;
    }

    configureStats(*bootstrap);
    configureStaticLayeredRuntime(*bootstrap);
    configureListenerManager(*bootstrap);

    if (auto status = static_cast<T*>(this)->configXds(bootstrap.get()); !status.ok()) {
      return status;
    }

    return bootstrap;
  }

protected:
  // The default implementation if the derived class does not override it.
  absl::Status configureTracing(::envoy::extensions::filters::network::http_connection_manager::v3::
                                    HttpConnectionManager_Tracing* tracing_config) {
    (void)tracing_config;
    return absl::OkStatus();
  }

  absl::Status configureHttpFilters(
      std::function<envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter*()>
          add_filter) {
    (void)add_filter;
    return absl::OkStatus();
  }

  absl::Status configXds(envoy::config::bootstrap::v3::Bootstrap* bootstrap) {
    (void)bootstrap;
    return absl::OkStatus();
  }

protected:
  bool useWorkerThread() const { return use_worker_thread_; }

  void setWatchdog(envoy::config::bootstrap::v3::Watchdogs watchdog) {
    watchdog_ = std::move(watchdog);
  }

  void addStatsInclusionPattern(envoy::type::matcher::v3::StringMatcher pattern) {
    if (!stats_inclusion_list_.has_value()) {
      stats_inclusion_list_.emplace();
    }
    *stats_inclusion_list_->add_patterns() = std::move(pattern);
  }

private:
  void configureStats(envoy::config::bootstrap::v3::Bootstrap& bootstrap) const {
    if (enable_stats_collection_) {
      if (stats_inclusion_list_.has_value()) {
        auto* list =
            bootstrap.mutable_stats_config()->mutable_stats_matcher()->mutable_inclusion_list();
        list->MergeFrom(stats_inclusion_list_.value());
      }
    } else {
      bootstrap.mutable_stats_config()->mutable_stats_matcher()->set_reject_all(true);
    }
    bootstrap.mutable_stats_config()->mutable_use_all_default_tags()->set_value(false);
  }

  void configureStaticLayeredRuntime(envoy::config::bootstrap::v3::Bootstrap& bootstrap) const {
    auto* runtime = bootstrap.mutable_layered_runtime()->add_layers();
    runtime->set_name("static_layer_0");
    Protobuf::Struct envoy_layer;
    Protobuf::Struct& runtime_values =
        *(*envoy_layer.mutable_fields())["envoy"].mutable_struct_value();

    if (!reloadable_runtime_guards_.empty()) {
      Protobuf::Struct& reloadable_features =
          *(*runtime_values.mutable_fields())["reloadable_features"].mutable_struct_value();
      for (const auto& guard_and_value : reloadable_runtime_guards_) {
        if (Runtime::RuntimeFeaturesDefaults::get().getFlag(absl::StrJoin(
                {"envoy", "reloadable_features", guard_and_value.first}, ".")) == nullptr) {
          continue;
        }
        (*reloadable_features.mutable_fields())[guard_and_value.first].set_bool_value(
            guard_and_value.second);
      }
    }

    if (!restart_runtime_guards_.empty()) {
      Protobuf::Struct& restart_features =
          *(*runtime_values.mutable_fields())["restart_features"].mutable_struct_value();
      for (const auto& guard_and_value : restart_runtime_guards_) {
        if (Runtime::RuntimeFeaturesDefaults::get().getFlag(absl::StrJoin(
                {"envoy", "restart_features", guard_and_value.first}, ".")) == nullptr) {
          continue;
        }
        (*restart_features.mutable_fields())[guard_and_value.first].set_bool_value(
            guard_and_value.second);
      }
    }

    (*runtime_values.mutable_fields())["disallow_global_stats"].set_bool_value(true);

    Protobuf::Struct& overload_values =
        *(*envoy_layer.mutable_fields())["overload"].mutable_struct_value();
    (*overload_values.mutable_fields())["global_downstream_max_connections"].set_string_value(
        "4294967295");

    runtime->mutable_static_layer()->MergeFrom(envoy_layer);
  }

  void configureListenerManager(envoy::config::bootstrap::v3::Bootstrap& bootstrap) const {
    envoy::config::bootstrap::v3::ApiListenerManager api;
    if (!useWorkerThread()) {
      api.set_threading_model(envoy::config::bootstrap::v3::ApiListenerManager::MAIN_THREAD_ONLY);
    } else {
      api.set_threading_model(
          envoy::config::bootstrap::v3::ApiListenerManager::STANDALONE_WORKER_THREAD);
    }
    auto* listener_manager = bootstrap.mutable_listener_manager();
    listener_manager->mutable_typed_config()->PackFrom(api);
    listener_manager->set_name("envoy.listener_manager_impl.api");
  }

  absl::optional<envoy::config::bootstrap::v3::Watchdogs> watchdog_;
  absl::optional<envoy::type::matcher::v3::ListStringMatcher> stats_inclusion_list_;
  uint32_t per_connection_buffer_limit_bytes_ = 10485760;
  int stream_idle_timeout_seconds_ = 15;
  std::vector<std::pair<std::string, bool>> reloadable_runtime_guards_;
  std::vector<std::pair<std::string, bool>> restart_runtime_guards_;

  // Common fields for InternalEngine
  Logger::Logger::Levels log_level_ = Logger::Logger::Levels::info;
  std::unique_ptr<EnvoyLogger> logger_{nullptr};
  bool enable_logger_{true};
  std::unique_ptr<EngineCallbacks> callbacks_;
  std::unique_ptr<EnvoyEventTracker> event_tracker_{nullptr};
  absl::optional<size_t> high_watermark_ = absl::nullopt;
  bool enable_stats_collection_ = true;
  bool use_worker_thread_ = false;
};

} // namespace Platform
} // namespace Envoy
