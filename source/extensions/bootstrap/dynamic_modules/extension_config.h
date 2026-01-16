#pragma once

#include <string>

#include "envoy/event/dispatcher.h"

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

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

class DynamicModuleBootstrapExtension;

/**
 * A config to create bootstrap extensions based on a dynamic module. This will be owned by the
 * bootstrap extension. This resolves and holds the symbols used for the bootstrap extension.
 */
class DynamicModuleBootstrapExtensionConfig
    : public std::enable_shared_from_this<DynamicModuleBootstrapExtensionConfig> {
public:
  /**
   * Constructor for the config.
   * @param extension_name the name of the extension.
   * @param extension_config the configuration for the module.
   * @param dynamic_module the dynamic module to use.
   * @param main_thread_dispatcher the main thread dispatcher.
   */
  DynamicModuleBootstrapExtensionConfig(const absl::string_view extension_name,
                                        const absl::string_view extension_config,
                                        Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                        Event::Dispatcher& main_thread_dispatcher);

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

  // The dynamic module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;

  // The main thread dispatcher.
  Event::Dispatcher& main_thread_dispatcher_;
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
 * @return an error status if the module could not be loaded or the configuration could not be
 * created, or a shared pointer to the config.
 */
absl::StatusOr<DynamicModuleBootstrapExtensionConfigSharedPtr>
newDynamicModuleBootstrapExtensionConfig(
    const absl::string_view extension_name, const absl::string_view extension_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Event::Dispatcher& main_thread_dispatcher);

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
