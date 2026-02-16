#pragma once

#include "envoy/common/callback.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/server/lifecycle_notifier.h"
#include "envoy/stats/store.h"

#include "source/common/common/logger.h"
#include "source/extensions/bootstrap/dynamic_modules/extension_config.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

/**
 * A bootstrap extension that uses a dynamic module.
 */
class DynamicModuleBootstrapExtension : public Server::BootstrapExtension,
                                        public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  DynamicModuleBootstrapExtension(DynamicModuleBootstrapExtensionConfigSharedPtr config);
  ~DynamicModuleBootstrapExtension() override;

  /**
   * Initializes the in-module extension.
   */
  void initializeInModuleExtension();

  // Server::BootstrapExtension
  void onServerInitialized() override;
  void onWorkerThreadInitialized() override;

  /**
   * Check if the extension has been destroyed.
   */
  bool isDestroyed() const { return destroyed_; }

  /**
   * Get the extension configuration.
   */
  const DynamicModuleBootstrapExtensionConfig& getExtensionConfig() const { return *config_; }

  /**
   * Get the stats store.
   */
  Stats::Store& statsStore() { return config_->stats_store_; }

  /**
   * Destroys the in-module extension. This is called by the destructor. It is safe to call
   * multiple times.
   */
  void destroy();

private:
  /**
   * Helper to get the `this` pointer as a void pointer.
   */
  void* thisAsVoidPtr() { return static_cast<void*>(this); }

  /**
   * Registers drain and shutdown lifecycle callbacks with the server.
   */
  void registerLifecycleCallbacks();

  // The configuration for this extension.
  DynamicModuleBootstrapExtensionConfigSharedPtr config_;

  // The in-module extension pointer.
  envoy_dynamic_module_type_bootstrap_extension_module_ptr in_module_extension_ = nullptr;

  // Whether the extension has been destroyed.
  bool destroyed_ = false;

  // Handle for the drain close callback registration. Dropped on destruction to unregister.
  Common::CallbackHandlePtr drain_handle_;

  // Handle for the shutdown lifecycle callback registration.
  Server::ServerLifecycleNotifier::HandlePtr shutdown_handle_;
};

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
