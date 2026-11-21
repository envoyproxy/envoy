#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "envoy/common/callback.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/server/lifecycle_notifier.h"
#include "envoy/stats/store.h"

#include "source/common/common/logger.h"
#include "source/extensions/bootstrap/dynamic_modules/extension_config.h"

#include "absl/container/btree_map.h"
#include "absl/types/variant.h"

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
   * Initializes the in-module extension. Returns false when the module returns a null extension.
   */
  bool initializeInModuleExtension();

  // Server::BootstrapExtension
  void onServerInitialized(Server::Instance&) override;
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

  /**
   * Starts calling the module's runtime update hooks when the value of the given runtime key
   * changes in a newly loaded runtime snapshot, compared with its value at the previous update, or
   * when this was called. Must be called on the main thread during
   * on_bootstrap_extension_server_initialized.
   * @param key the runtime key to watch.
   * @return false if the key is empty or already watched, if the module implements none of the
   * runtime update hooks, outside of on_bootstrap_extension_server_initialized, or once shutdown
   * began.
   */
  bool enableRuntimeUpdate(absl::string_view key);

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

  // Handle for the shutdown lifecycle callback that notifies the configuration of shutdown.
  Server::ServerLifecycleNotifier::HandlePtr config_shutdown_handle_;

  // Handle for the lifecycle callback that notifies the configuration before the workers start.
  Server::ServerLifecycleNotifier::HandlePtr post_init_handle_;

  // The value of a watched runtime key, typed the way the runtime update hooks report it: a
  // boolean, a non-negative integer or any other number.
  using RuntimeValue = absl::variant<bool, uint64_t, double>;

  // Returns the value of the key from the topmost layer of the snapshot that sets it, or nothing
  // when that value is neither a boolean nor a number.
  static std::optional<RuntimeValue> runtimeValue(const Runtime::Snapshot& snapshot,
                                                  absl::string_view key);

  // Like operator==, with NaN equal to itself so that a key that stays NaN is not reported.
  static bool sameRuntimeValue(const std::optional<RuntimeValue>& a,
                               const std::optional<RuntimeValue>& b);

  // Calls the module's runtime update hook of the type of the value.
  void reportRuntimeUpdate(absl::string_view key, const RuntimeValue& value, bool removed);

  // Called by the runtime on the main thread once it loaded a new snapshot.
  void onRuntimeUpdate();

  // Set while on_bootstrap_extension_server_initialized runs, the only time the module can watch
  // runtime keys, so that the runtime update hook cannot change the watched keys while Envoy
  // iterates over them.
  bool in_server_initialized_ = false;
  // Set when the module watches its first runtime key via enableRuntimeUpdate().
  Common::CallbackHandlePtr runtime_update_handle_;
  // The watched runtime keys and their values at the previous update, ordered by key.
  absl::btree_map<std::string, std::optional<RuntimeValue>> watched_runtime_values_;
};

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
