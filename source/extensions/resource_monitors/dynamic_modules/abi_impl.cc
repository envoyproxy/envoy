// NOLINT(namespace-envoy)

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/resource_monitors/dynamic_modules/resource_monitor.h"

#include "absl/strings/str_cat.h"

namespace {

using Envoy::Extensions::ResourceMonitors::DynamicModules::DynamicModuleResourceMonitorConfig;
using Envoy::Extensions::ResourceMonitors::DynamicModules::
    DynamicModuleResourceMonitorConfigScheduler;

DynamicModuleResourceMonitorConfigScheduler* toResourceMonitorConfigScheduler(
    envoy_dynamic_module_type_resource_monitor_config_scheduler_module_ptr ptr) {
  return static_cast<DynamicModuleResourceMonitorConfigScheduler*>(ptr);
}

} // namespace

extern "C" {

bool envoy_dynamic_module_callback_resource_monitor_config_enable_synchronous_feedback(
    envoy_dynamic_module_type_resource_monitor_config_envoy_ptr config_envoy_ptr) {
  if (!Envoy::Thread::MainThread::isMainOrTestThread()) {
    IS_ENVOY_BUG(absl::StrCat(__func__, " must be called on the main thread"));
    return false;
  }
  return static_cast<DynamicModuleResourceMonitorConfig*>(config_envoy_ptr)
      ->enableSynchronousFeedback();
}

envoy_dynamic_module_type_resource_monitor_config_scheduler_module_ptr
envoy_dynamic_module_callback_resource_monitor_config_scheduler_new(
    envoy_dynamic_module_type_resource_monitor_config_envoy_ptr config_envoy_ptr) {
  return new DynamicModuleResourceMonitorConfigScheduler(
      static_cast<DynamicModuleResourceMonitorConfig*>(config_envoy_ptr)->scheduler_target_);
}

void envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_resource_usage(
    envoy_dynamic_module_type_resource_monitor_config_scheduler_module_ptr scheduler_module_ptr,
    double resource_pressure) {
  toResourceMonitorConfigScheduler(scheduler_module_ptr)->reportResourceUsage(resource_pressure);
}

void envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_failure(
    envoy_dynamic_module_type_resource_monitor_config_scheduler_module_ptr scheduler_module_ptr) {
  toResourceMonitorConfigScheduler(scheduler_module_ptr)->reportFailure();
}

void envoy_dynamic_module_callback_resource_monitor_config_scheduler_delete(
    envoy_dynamic_module_type_resource_monitor_config_scheduler_module_ptr scheduler_module_ptr) {
  delete toResourceMonitorConfigScheduler(scheduler_module_ptr);
}

} // extern "C"
