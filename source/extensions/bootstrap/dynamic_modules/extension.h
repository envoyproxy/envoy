#pragma once

#include "envoy/server/bootstrap_extension_config.h"
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

private:
  /**
   * Helper to get the `this` pointer as a void pointer.
   */
  void* thisAsVoidPtr() { return static_cast<void*>(this); }

  /**
   * Destroys the in-module extension.
   */
  void destroy();

  // The configuration for this extension.
  DynamicModuleBootstrapExtensionConfigSharedPtr config_;

  // The in-module extension pointer.
  envoy_dynamic_module_type_bootstrap_extension_module_ptr in_module_extension_ = nullptr;

  // Whether the extension has been destroyed.
  bool destroyed_ = false;
};

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
