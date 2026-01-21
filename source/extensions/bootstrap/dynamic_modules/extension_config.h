#pragma once

#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/http/async_client.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/store.h"

#include "source/common/common/logger.h"
#include "source/common/http/message_impl.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

using OnBootstrapExtensionConfigDestroyType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_config_destroy);
using OnBootstrapExtensionNewType = decltype(&envoy_dynamic_module_on_bootstrap_extension_new);
using OnBootstrapExtensionServerInitializedType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_server_initialized);
using OnBootstrapExtensionWorkerThreadInitializedType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized);
using OnBootstrapExtensionDestroyType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_destroy);
using OnBootstrapExtensionConfigScheduledType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_config_scheduled);
using OnBootstrapExtensionHttpCalloutDoneType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_http_callout_done);

class DynamicModuleBootstrapExtension;

/**
 * A config to create bootstrap extensions based on a dynamic module. This will be owned by the
 * bootstrap extension. This resolves and holds the symbols used for the bootstrap extension.
 */
class DynamicModuleBootstrapExtensionConfig
    : public std::enable_shared_from_this<DynamicModuleBootstrapExtensionConfig>,
      public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  /**
   * Constructor for the config.
   * @param extension_name the name of the extension.
   * @param extension_config the configuration for the module.
   * @param dynamic_module the dynamic module to use.
   * @param main_thread_dispatcher the main thread dispatcher.
   * @param context the server factory context for accessing cluster manager lazily.
   * @param stats_store the stats store for accessing metrics.
   */
  DynamicModuleBootstrapExtensionConfig(const absl::string_view extension_name,
                                        const absl::string_view extension_config,
                                        Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                        Event::Dispatcher& main_thread_dispatcher,
                                        Server::Configuration::ServerFactoryContext& context,
                                        Stats::Store& stats_store);

  ~DynamicModuleBootstrapExtensionConfig();

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
  OnBootstrapExtensionConfigScheduledType on_bootstrap_extension_config_scheduled_ = nullptr;
  OnBootstrapExtensionHttpCalloutDoneType on_bootstrap_extension_http_callout_done_ = nullptr;

  // The dynamic module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;

  // The main thread dispatcher.
  Event::Dispatcher& main_thread_dispatcher_;

  // The server factory context for accessing cluster manager lazily. ClusterManager is not
  // available during bootstrap extension creation, so we store the context and access it when
  // needed.
  Server::Configuration::ServerFactoryContext& context_;

  // The stats store for accessing metrics.
  Stats::Store& stats_store_;

private:
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
  DynamicModuleBootstrapExtensionConfigScheduler(
      std::weak_ptr<DynamicModuleBootstrapExtensionConfig> config, Event::Dispatcher& dispatcher)
      : config_(std::move(config)), dispatcher_(dispatcher) {}

  void commit(uint64_t event_id) {
    dispatcher_.post([config = config_, event_id]() {
      if (std::shared_ptr<DynamicModuleBootstrapExtensionConfig> config_shared = config.lock()) {
        config_shared->onScheduled(event_id);
      }
    });
  }

private:
  // The config that this scheduler is associated with. Using a weak pointer to avoid unnecessarily
  // extending the lifetime of the config.
  std::weak_ptr<DynamicModuleBootstrapExtensionConfig> config_;
  // The dispatcher is used to post the event to the main thread.
  Event::Dispatcher& dispatcher_;
};

/**
 * Creates a new DynamicModuleBootstrapExtensionConfig from the given module and configuration.
 * @param extension_name the name of the extension.
 * @param extension_config the configuration for the module.
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
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Event::Dispatcher& main_thread_dispatcher, Server::Configuration::ServerFactoryContext& context,
    Stats::Store& stats_store);

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
