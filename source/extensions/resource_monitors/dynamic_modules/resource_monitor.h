#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/server/resource_monitor.h"

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DynamicModules {

using OnResourceMonitorConfigNewType =
    decltype(&envoy_dynamic_module_on_resource_monitor_config_new);
using OnResourceMonitorConfigDestroyType =
    decltype(&envoy_dynamic_module_on_resource_monitor_config_destroy);
using OnResourceMonitorUpdateResourceUsageType =
    decltype(&envoy_dynamic_module_on_resource_monitor_update_resource_usage);
using OnResourceMonitorGetResourceUsageType =
    decltype(&envoy_dynamic_module_on_resource_monitor_get_resource_usage);
using OnResourceMonitorLoadAcceptedType =
    decltype(&envoy_dynamic_module_on_resource_monitor_load_accepted);

class DynamicModuleResourceMonitorConfig;

/**
 * The state that a resource monitor configuration shares with its schedulers. The schedulers do
 * not hold the configuration, so it is always destroyed on the main thread.
 */
struct DynamicModuleResourceMonitorSchedulerTarget {
  explicit DynamicModuleResourceMonitorSchedulerTarget(Event::Dispatcher& main_thread_dispatcher)
      : main_thread_dispatcher_(&main_thread_dispatcher) {}

  absl::Mutex mutex_;
  // Cleared when the configuration is destroyed, after which reports are dropped.
  Event::Dispatcher* main_thread_dispatcher_ ABSL_GUARDED_BY(mutex_);
  // Set when the configuration is created, before the module can create a scheduler.
  std::weak_ptr<DynamicModuleResourceMonitorConfig> config_;
};

/**
 * The loaded module, the resolved event hooks and the update state of a dynamic module resource
 * monitor. The module receives a pointer to this object when it creates its configuration and at
 * each update, and passes it to the resource monitor callbacks.
 */
class DynamicModuleResourceMonitorConfig {
public:
  DynamicModuleResourceMonitorConfig(Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                     Event::Dispatcher& main_thread_dispatcher);
  ~DynamicModuleResourceMonitorConfig();

  /**
   * Asks the module for the pressure of the resource and reports it to the callbacks, right away or
   * once the module reports the update it left pending.
   */
  void updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks);

  /**
   * Reports the pressure of the pending update, if one is pending.
   */
  void reportResourceUsage(double resource_pressure);

  /**
   * Fails the pending update, if one is pending.
   */
  void reportFailure();

  /**
   * Enables synchronous feedback.
   * @return true if synchronous feedback is enabled, false if the module is not creating its
   * configuration.
   */
  bool enableSynchronousFeedback();

  // The in-module configuration and the event hooks are set during creation and never change.
  envoy_dynamic_module_type_resource_monitor_config_module_ptr in_module_config_{nullptr};
  OnResourceMonitorConfigDestroyType on_config_destroy_{nullptr};
  OnResourceMonitorUpdateResourceUsageType on_update_resource_usage_{nullptr};
  OnResourceMonitorGetResourceUsageType on_get_resource_usage_{nullptr};
  // Null if the module does not implement the optional load accepted hook.
  OnResourceMonitorLoadAcceptedType on_load_accepted_{nullptr};
  // Whether envoy_dynamic_module_on_resource_monitor_config_new is running, which is the only time
  // the module can enable synchronous feedback.
  bool in_config_new_{false};
  bool synchronous_feedback_enabled_{false};

  const std::shared_ptr<DynamicModuleResourceMonitorSchedulerTarget> scheduler_target_;

private:
  const Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
  // The callbacks of the update that the module left pending, or nullptr if none is pending.
  Server::ResourceUpdateCallbacks* pending_update_callbacks_{nullptr};
};

using DynamicModuleResourceMonitorConfigSharedPtr =
    std::shared_ptr<DynamicModuleResourceMonitorConfig>;

/**
 * Reports the outcome of a pending update from any thread to a resource monitor configuration on
 * the main thread.
 */
class DynamicModuleResourceMonitorConfigScheduler {
public:
  explicit DynamicModuleResourceMonitorConfigScheduler(
      std::shared_ptr<DynamicModuleResourceMonitorSchedulerTarget> target)
      : target_(std::move(target)) {}

  void reportResourceUsage(double resource_pressure);
  void reportFailure();

private:
  // Applies the report to the configuration on the main thread, unless it is destroyed first.
  void post(absl::AnyInvocable<void(DynamicModuleResourceMonitorConfig&)> report);

  const std::shared_ptr<DynamicModuleResourceMonitorSchedulerTarget> target_;
};

/**
 * Resolves the event hooks of the module and creates the in-module configuration. Must be called on
 * the main thread.
 * @param resource_monitor_name the name identifying the implementation within the module.
 * @param resource_monitor_config the configuration bytes for the implementation.
 * @param dynamic_module the loaded dynamic module.
 * @param main_thread_dispatcher the dispatcher of the main thread.
 * @return the configuration, or an error if a required event hook is missing or the module fails to
 * create its configuration.
 */
absl::StatusOr<DynamicModuleResourceMonitorConfigSharedPtr>
newDynamicModuleResourceMonitorConfig(absl::string_view resource_monitor_name,
                                      absl::string_view resource_monitor_config,
                                      Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                      Event::Dispatcher& main_thread_dispatcher);

/**
 * Resource monitor backed by a dynamic module.
 */
class DynamicModuleResourceMonitor : public Server::ResourceMonitor {
public:
  explicit DynamicModuleResourceMonitor(DynamicModuleResourceMonitorConfigSharedPtr config)
      : config_(std::move(config)) {}

  // Server::ResourceMonitor
  void updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) override {
    config_->updateResourceUsage(callbacks);
  }

private:
  const DynamicModuleResourceMonitorConfigSharedPtr config_;
};

/**
 * Resource monitor backed by a dynamic module that enabled synchronous feedback.
 */
class DynamicModuleSynchronousFeedbackResourceMonitor
    : public Server::SynchronousFeedbackResourceMonitor {
public:
  explicit DynamicModuleSynchronousFeedbackResourceMonitor(
      DynamicModuleResourceMonitorConfigSharedPtr config)
      : config_(std::move(config)) {}

  // Server::SynchronousFeedbackResourceMonitor
  Server::ResourceUsage getResourceUsage() override;
  void onLoadAccepted(absl::string_view load_shed_point_name) override;

private:
  const DynamicModuleResourceMonitorConfigSharedPtr config_;
};

} // namespace DynamicModules
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
