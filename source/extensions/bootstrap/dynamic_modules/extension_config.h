#pragma once

#include <string>
#include <vector>

#include "envoy/common/callback.h"
#include "envoy/common/optref.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/watcher.h"
#include "envoy/http/async_client.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/listener_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/store.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/http/message_impl.h"
#include "source/common/init/target_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/dynamic_modules/metric_registry.h"

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

// The default custom stat namespace which prepends all user-defined metrics.
// This can be overridden via the ``metrics_namespace`` field in ``DynamicModuleConfig``.
constexpr absl::string_view DefaultMetricsNamespace = "dynamicmodulescustom";

using OnBootstrapExtensionConfigDestroyType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_config_destroy);
using OnBootstrapExtensionNewType = decltype(&envoy_dynamic_module_on_bootstrap_extension_new);
using OnBootstrapExtensionServerInitializedType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_server_initialized);
using OnBootstrapExtensionWorkerThreadInitializedType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized);
using OnBootstrapExtensionDestroyType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_destroy);
using OnBootstrapExtensionDrainStartedType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_drain_started);
using OnBootstrapExtensionShutdownType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_shutdown);
using OnBootstrapExtensionConfigScheduledType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_config_scheduled);
using OnBootstrapExtensionHttpCalloutDoneType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_http_callout_done);
using OnBootstrapExtensionTimerFiredType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_timer_fired);
using OnBootstrapExtensionFileChangedType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_file_changed);
using OnBootstrapExtensionAdminRequestType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_admin_request);
using OnBootstrapExtensionClusterAddOrUpdateType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_cluster_add_or_update);
using OnBootstrapExtensionClusterRemovalType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_cluster_removal);
using OnBootstrapExtensionListenerAddOrUpdateType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_listener_add_or_update);
using OnBootstrapExtensionListenerRemovalType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_listener_removal);
using OnBootstrapExtensionSecretAddOrUpdateType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_secret_add_or_update);
using OnBootstrapExtensionSecretRemovalType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_secret_removal);

class DynamicModuleBootstrapExtension;

/**
 * A config to create bootstrap extensions based on a dynamic module. This will be owned by the
 * bootstrap extension. This resolves and holds the symbols used for the bootstrap extension.
 */
class DynamicModuleBootstrapExtensionConfig
    : public std::enable_shared_from_this<DynamicModuleBootstrapExtensionConfig>,
      public Upstream::ClusterUpdateCallbacks,
      public Server::ListenerUpdateCallbacks,
      public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  /**
   * Constructor for the config.
   * @param extension_name the name of the extension.
   * @param extension_config the configuration for the module.
   * @param metrics_namespace the namespace prefix for metrics emitted by this module.
   * @param dynamic_module the dynamic module to use.
   * @param main_thread_dispatcher the main thread dispatcher.
   * @param context the server factory context for accessing cluster manager lazily.
   * @param stats_store the stats store for accessing metrics.
   */
  DynamicModuleBootstrapExtensionConfig(const absl::string_view extension_name,
                                        const absl::string_view extension_config,
                                        const absl::string_view metrics_namespace,
                                        Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                        Event::Dispatcher& main_thread_dispatcher,
                                        Server::Configuration::ServerFactoryContext& context,
                                        Stats::Store& stats_store);

  ~DynamicModuleBootstrapExtensionConfig() override;

  /**
   * This is called when an event is scheduled via
   * DynamicModuleBootstrapExtensionConfigScheduler::commit.
   */
  void onScheduled(uint64_t event_id);

  /**
   * Helper to get the `this` pointer as a void pointer.
   */
  void* thisAsVoidPtr() { return static_cast<void*>(this); }

  /**
   * Sends an HTTP callout to the specified cluster with the given message.
   * This must be called on the main thread.
   *
   * @param callout_id_out is a pointer to a variable where the callout ID will be stored.
   * @param cluster_name is the name of the cluster to which the callout is sent.
   * @param message is the HTTP request message to send.
   * @param timeout_milliseconds is the timeout for the callout in milliseconds.
   * @return the result of the callout initialization.
   */
  envoy_dynamic_module_type_http_callout_init_result
  sendHttpCallout(uint64_t* callout_id_out, absl::string_view cluster_name,
                  Http::RequestMessagePtr&& message, uint64_t timeout_milliseconds);

  /**
   * Signals that the module's initialization is complete. This unblocks the init manager and
   * allows Envoy to start accepting traffic. An init target is automatically registered for every
   * bootstrap extension, so the module must call this exactly once to unblock startup.
   */
  void signalInitComplete();

  /**
   * Enables cluster lifecycle event notifications. When enabled, the module will receive
   * on_bootstrap_extension_cluster_add_or_update and on_bootstrap_extension_cluster_removal
   * callbacks when clusters are added, updated, or removed.
   *
   * This must be called on the main thread after the server is initialized, since the
   * ClusterManager is not available during bootstrap extension creation.
   *
   * @return true if the callbacks were successfully registered, false if already registered.
   */
  bool enableClusterLifecycle();

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(absl::string_view cluster_name,
                            Upstream::ThreadLocalClusterCommand& get_cluster) override;
  void onClusterRemoval(absl::string_view cluster_name) override;

  /**
   * Sets the listener manager reference. Must be called during onServerInitialized before
   * the module can enable listener lifecycle events. Marks the server as initialized so the
   * cluster manager can be accessed safely.
   */
  void setListenerManager(Server::ListenerManager& listener_manager) {
    listener_manager_ = &listener_manager;
    server_initialized_ = true;
  }

  /**
   * Enables listener lifecycle event notifications. When enabled, the module will receive
   * on_bootstrap_extension_listener_add_or_update and on_bootstrap_extension_listener_removal
   * callbacks when listeners are added, updated, or removed.
   *
   * This must be called on the main thread after the server is initialized, since the
   * ListenerManager is not available during bootstrap extension creation.
   *
   * @return true if the callbacks were successfully registered, false if already registered.
   */
  bool enableListenerLifecycle();

  // Server::ListenerUpdateCallbacks
  void onListenerAddOrUpdate(absl::string_view listener_name,
                             const Network::ListenerConfig& listener_config) override;
  void onListenerRemoval(const std::string& listener_name) override;

  /**
   * Enables secret lifecycle event notifications. When enabled, the module receives
   * on_bootstrap_extension_secret_add_or_update when a dynamic TLS certificate secret becomes
   * active or is rotated, and on_bootstrap_extension_secret_removal when one is removed. SDS
   * secrets are independent xDS resources, so these fire even when no cluster or listener is
   * re-pushed.
   *
   * Secrets whose providers already exist and are active at enable time are replayed immediately,
   * so a module that enables late still observes them. This must be called on the main thread after
   * the server is initialized, since the SecretManager is not available before that point.
   *
   * @return true if the callbacks were successfully registered, false if already registered.
   */
  bool enableSecretLifecycle();

  /**
   * Enumerates the names of the currently active resources of a single kind: the active listeners'
   * filter chains (inline and FCDS), the clusters, their transport socket matches, or the active
   * dynamic TLS certificate secrets. `emit` is invoked once per name. A no-op before the server is
   * initialized. Main thread only.
   *
   * @param kind selects which kind of active resource to enumerate.
   * @param emit is invoked for each name; the string_view is valid only during that invocation.
   */
  void getActiveResourceNames(envoy_dynamic_module_type_bootstrap_active_resource_kind kind,
                              absl::FunctionRef<void(absl::string_view)> emit);

  /**
   * Helper function to compute the transport socket match names present in every cluster that has
   * matches. A match is observed only once it is in all such clusters; clusters with no matches do
   * not constrain the result. Exposed for testing.
   */
  static std::vector<absl::string_view> transportSocketMatchIntersection(
      const std::vector<std::vector<absl::string_view>>& per_cluster_matches);

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_bootstrap_extension_config_module_ptr in_module_config_ = nullptr;

  // The function pointers for the module related to the bootstrap extension. All of them are
  // resolved during the construction of the config and made sure they are not nullptr after that.

  OnBootstrapExtensionConfigDestroyType on_bootstrap_extension_config_destroy_ = nullptr;
  OnBootstrapExtensionNewType on_bootstrap_extension_new_ = nullptr;
  OnBootstrapExtensionServerInitializedType on_bootstrap_extension_server_initialized_ = nullptr;
  OnBootstrapExtensionWorkerThreadInitializedType
      on_bootstrap_extension_worker_thread_initialized_ = nullptr;
  OnBootstrapExtensionDestroyType on_bootstrap_extension_destroy_ = nullptr;
  OnBootstrapExtensionDrainStartedType on_bootstrap_extension_drain_started_ = nullptr;
  OnBootstrapExtensionShutdownType on_bootstrap_extension_shutdown_ = nullptr;
  OnBootstrapExtensionConfigScheduledType on_bootstrap_extension_config_scheduled_ = nullptr;
  OnBootstrapExtensionHttpCalloutDoneType on_bootstrap_extension_http_callout_done_ = nullptr;
  OnBootstrapExtensionTimerFiredType on_bootstrap_extension_timer_fired_ = nullptr;
  OnBootstrapExtensionFileChangedType on_bootstrap_extension_file_changed_ = nullptr;
  OnBootstrapExtensionAdminRequestType on_bootstrap_extension_admin_request_ = nullptr;
  OnBootstrapExtensionClusterAddOrUpdateType on_bootstrap_extension_cluster_add_or_update_ =
      nullptr;
  OnBootstrapExtensionClusterRemovalType on_bootstrap_extension_cluster_removal_ = nullptr;
  OnBootstrapExtensionListenerAddOrUpdateType on_bootstrap_extension_listener_add_or_update_ =
      nullptr;
  OnBootstrapExtensionListenerRemovalType on_bootstrap_extension_listener_removal_ = nullptr;
  OnBootstrapExtensionSecretAddOrUpdateType on_bootstrap_extension_secret_add_or_update_ = nullptr;
  OnBootstrapExtensionSecretRemovalType on_bootstrap_extension_secret_removal_ = nullptr;

  // The dynamic module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;

  // The main thread dispatcher.
  Event::Dispatcher& main_thread_dispatcher_;

  // File watchers created by
  // envoy_dynamic_module_callback_bootstrap_extension_file_watcher_add_watch. Envoy owns the
  // lifetime, so watchers are destroyed when the config is destroyed.
  std::vector<Filesystem::WatcherPtr> file_watchers_;

  // The server factory context for accessing cluster manager lazily. ClusterManager is not
  // available during bootstrap extension creation, so we store the context and access it when
  // needed.
  Server::Configuration::ServerFactoryContext& context_;

  // The stats store for accessing metrics.
  Stats::Store& stats_store_;

  // The init target for blocking Envoy startup until the module signals readiness.
  // Created during config construction and registered with the init manager.
  std::unique_ptr<Init::TargetImpl> init_target_;

  // ----------------------------- Metrics Support -----------------------------
  // The shared registry holding all module-defined metrics.
  Extensions::DynamicModules::MetricRegistry& metrics() { return metrics_; }

  // Owns the scope the registry references. Must precede metrics_ so it initializes first.
  const Stats::ScopeSharedPtr stats_scope_;
  // Shared metrics registry composed from stats_scope_.
  Extensions::DynamicModules::MetricRegistry metrics_;
  // We only allow the module to create stats during on_bootstrap_extension_config_new, and not
  // later from worker threads, so that we don't have to wrap the metrics registry pool in a lock.
  // Per-request label values use a stack-local Stats::StatNameDynamicPool in the increment
  // callbacks (see abi_impl.cc).
  bool stat_creation_frozen_ = false;

  // Temporary storage for the admin response body. Set by the
  // envoy_dynamic_module_callback_bootstrap_extension_admin_set_response callback during
  // on_bootstrap_extension_admin_request, then consumed by the admin handler lambda.
  std::string admin_response_body_;

private:
  // Subscribes to a dynamic TLS certificate secret provider's update and remove callbacks so the
  // module is notified when its secret becomes active/rotates or is removed. The subscription
  // handles are retained for the life of the config. Main thread only.
  void subscribeSecretProvider(const std::string& secret_name,
                               const Secret::TlsCertificateConfigProviderSharedPtr& provider);
  // Notifies the module that a dynamic secret became active/was updated or was removed.
  void onSecretAddOrUpdate(const std::string& secret_name);
  void onSecretRemoval(const std::string& secret_name);

  /**
   * This implementation of the AsyncClient::Callbacks is used to handle the response from the HTTP
   * callout from the parent bootstrap extension config.
   */
  class HttpCalloutCallback : public Http::AsyncClient::Callbacks {
  public:
    HttpCalloutCallback(std::shared_ptr<DynamicModuleBootstrapExtensionConfig> config, uint64_t id)
        : config_(std::move(config)), callout_id_(id) {}
    ~HttpCalloutCallback() override = default;

    void onSuccess(const Http::AsyncClient::Request& request,
                   Http::ResponseMessagePtr&& response) override;
    void onFailure(const Http::AsyncClient::Request& request,
                   Http::AsyncClient::FailureReason reason) override;
    void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                      const Http::ResponseHeaderMap*) override {};

    // This is the request object that is used to send the HTTP callout. It is used to cancel the
    // callout if the config is destroyed before the callout is completed.
    Http::AsyncClient::Request* request_ = nullptr;

  private:
    const std::shared_ptr<DynamicModuleBootstrapExtensionConfig> config_;
    const uint64_t callout_id_{};
  };

  uint64_t getNextCalloutId() { return next_callout_id_++; }

  uint64_t next_callout_id_ = 1; // 0 is reserved as an invalid id.

  absl::flat_hash_map<uint64_t,
                      std::unique_ptr<DynamicModuleBootstrapExtensionConfig::HttpCalloutCallback>>
      http_callouts_;

  // Cluster lifecycle callback handle. Set when the module enables cluster lifecycle events
  // via enableClusterLifecycle(). Reset during shutdown to avoid use-after-free since the
  // underlying TLS data is destroyed before the config.
  Upstream::ClusterUpdateCallbacksHandlePtr cluster_update_callbacks_handle_;
  // Handle for the shutdown lifecycle callback that cleans up cluster_update_callbacks_handle_.
  Server::ServerLifecycleNotifier::HandlePtr cluster_lifecycle_shutdown_handle_;
  bool cluster_lifecycle_enabled_ = false;

  // True once the server is initialized, set when the listener manager is provided during
  // onServerInitialized. The cluster manager is only safe to access after this point.
  bool server_initialized_ = false;

  // Listener manager pointer. Set during onServerInitialized via setListenerManager().
  // Not available during bootstrap extension creation.
  Server::ListenerManager* listener_manager_ = nullptr;

  // Listener lifecycle callback handle. Set when the module enables listener lifecycle events
  // via enableListenerLifecycle(). Reset during shutdown to avoid use-after-free since the
  // ListenerManager is destroyed before the config.
  Server::ListenerUpdateCallbacksHandlePtr listener_update_callbacks_handle_;
  // Handle for the shutdown lifecycle callback that cleans up listener_update_callbacks_handle_.
  Server::ServerLifecycleNotifier::HandlePtr listener_lifecycle_shutdown_handle_;
  bool listener_lifecycle_enabled_ = false;

  // Per-provider secret update/remove subscription handles, kept alive for the life of the config
  // so the module keeps receiving secret lifecycle events. Set when the module enables secret
  // lifecycle events via enableSecretLifecycle().
  std::vector<Common::CallbackHandlePtr> secret_callback_handles_;
  bool secret_lifecycle_enabled_ = false;
};

using DynamicModuleBootstrapExtensionConfigSharedPtr =
    std::shared_ptr<DynamicModuleBootstrapExtensionConfig>;

/**
 * This class is used to schedule a bootstrap extension config event hook from a different thread
 * than the main thread. This is created via
 * envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new and deleted via
 * envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete.
 */
class DynamicModuleBootstrapExtensionConfigScheduler {
public:
  explicit DynamicModuleBootstrapExtensionConfigScheduler(
      std::weak_ptr<DynamicModuleBootstrapExtensionConfig> config)
      : config_(std::move(config)) {}

  void commit(uint64_t event_id) {
    // Lock the config so its dispatcher member stays valid across `post`.
    auto config_shared = config_.lock();
    if (!config_shared) {
      return;
    }
    config_shared->main_thread_dispatcher_.post([config = config_, event_id]() {
      if (std::shared_ptr<DynamicModuleBootstrapExtensionConfig> cs = config.lock()) {
        cs->onScheduled(event_id);
      }
    });
  }

private:
  // The config that this scheduler is associated with. Using a weak pointer to avoid unnecessarily
  // extending the lifetime of the config.
  std::weak_ptr<DynamicModuleBootstrapExtensionConfig> config_;
};

/**
 * This class wraps an Envoy timer for use by bootstrap extension dynamic modules. It is created via
 * envoy_dynamic_module_callback_bootstrap_extension_timer_new and deleted via
 * envoy_dynamic_module_callback_bootstrap_extension_timer_delete.
 *
 * When the timer fires, it invokes the on_bootstrap_extension_timer_fired event hook on the main
 * thread if the config is still alive.
 */
class DynamicModuleBootstrapExtensionTimer {
public:
  explicit DynamicModuleBootstrapExtensionTimer(
      std::weak_ptr<DynamicModuleBootstrapExtensionConfig> config)
      : config_(std::move(config)) {}

  /**
   * Set the underlying Envoy timer. This is separated from construction to allow the timer
   * callback to capture a stable pointer to this object.
   */
  void setTimer(Event::TimerPtr timer) { timer_ = std::move(timer); }

  Event::Timer& timer() { return *timer_; }

private:
  // The config that this timer is associated with. Using a weak pointer to avoid unnecessarily
  // extending the lifetime of the config.
  std::weak_ptr<DynamicModuleBootstrapExtensionConfig> config_;
  // The underlying Envoy timer.
  Event::TimerPtr timer_;
};

/**
 * Creates a new DynamicModuleBootstrapExtensionConfig from the given module and configuration.
 * @param extension_name the name of the extension.
 * @param extension_config the configuration for the module.
 * @param metrics_namespace the namespace prefix for metrics emitted by this module.
 * @param dynamic_module the dynamic module to use.
 * @param main_thread_dispatcher the main thread dispatcher.
 * @param context the server factory context for accessing cluster manager lazily.
 * @param stats_store the stats store for accessing metrics.
 * @return an error status if the module could not be loaded or the configuration could not be
 * created, or a shared pointer to the config.
 */
absl::StatusOr<DynamicModuleBootstrapExtensionConfigSharedPtr>
newDynamicModuleBootstrapExtensionConfig(
    const absl::string_view extension_name, const absl::string_view extension_config,
    const absl::string_view metrics_namespace,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Event::Dispatcher& main_thread_dispatcher, Server::Configuration::ServerFactoryContext& context,
    Stats::Store& stats_store);

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
