#include "source/extensions/resource_monitors/dynamic_modules/resource_monitor.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DynamicModules {
namespace {

constexpr absl::string_view ModuleFailureMessage = "the module reported a failure";

// Every trigger saturates at 1, so a larger pressure is reported as 1. This also keeps the
// percentage that the overload manager records for the resource in range.
void reportPressure(Server::ResourceUpdateCallbacks& callbacks, double resource_pressure) {
  if (!std::isfinite(resource_pressure) || resource_pressure < 0) {
    callbacks.onFailure(
        absl::InvalidArgumentError(absl::StrCat("invalid resource pressure ", resource_pressure)));
    return;
  }
  callbacks.onSuccess({std::min(resource_pressure, 1.0)});
}

} // namespace

DynamicModuleResourceMonitorConfig::DynamicModuleResourceMonitorConfig(
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Event::Dispatcher& main_thread_dispatcher)
    : scheduler_target_(
          std::make_shared<DynamicModuleResourceMonitorSchedulerTarget>(main_thread_dispatcher)),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleResourceMonitorConfig::~DynamicModuleResourceMonitorConfig() {
  {
    absl::MutexLock lock(&scheduler_target_->mutex_);
    scheduler_target_->main_thread_dispatcher_ = nullptr;
  }
  if (in_module_config_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

void DynamicModuleResourceMonitorConfig::updateResourceUsage(
    Server::ResourceUpdateCallbacks& callbacks) {
  // The overload manager does not refresh a resource while its update is pending.
  ASSERT(pending_update_callbacks_ == nullptr);
  double resource_pressure = 0;
  switch (
      on_update_resource_usage_(static_cast<void*>(this), in_module_config_, &resource_pressure)) {
  case envoy_dynamic_module_type_resource_monitor_update_status_Success:
    reportPressure(callbacks, resource_pressure);
    return;
  case envoy_dynamic_module_type_resource_monitor_update_status_Failure:
    callbacks.onFailure(absl::InternalError(ModuleFailureMessage));
    return;
  case envoy_dynamic_module_type_resource_monitor_update_status_Pending:
    pending_update_callbacks_ = &callbacks;
    return;
  }
  callbacks.onFailure(absl::InternalError("the module returned an unknown update status"));
}

void DynamicModuleResourceMonitorConfig::reportResourceUsage(double resource_pressure) {
  if (pending_update_callbacks_ != nullptr) {
    reportPressure(*std::exchange(pending_update_callbacks_, nullptr), resource_pressure);
  }
}

void DynamicModuleResourceMonitorConfig::reportFailure() {
  if (pending_update_callbacks_ != nullptr) {
    std::exchange(pending_update_callbacks_, nullptr)
        ->onFailure(absl::InternalError(ModuleFailureMessage));
  }
}

bool DynamicModuleResourceMonitorConfig::enableSynchronousFeedback() {
  if (!in_config_new_) {
    return false;
  }
  synchronous_feedback_enabled_ = true;
  return true;
}

Server::ResourceUsage DynamicModuleSynchronousFeedbackResourceMonitor::getResourceUsage() {
  const double resource_pressure = config_->on_get_resource_usage_(config_->in_module_config_);
  // A load shed point cannot fail its check, so a negative or NaN pressure counts as no pressure.
  return {std::isnan(resource_pressure) ? 0 : std::clamp(resource_pressure, 0.0, 1.0)};
}

void DynamicModuleSynchronousFeedbackResourceMonitor::onLoadAccepted(
    absl::string_view load_shed_point_name) {
  if (config_->on_load_accepted_ != nullptr) {
    config_->on_load_accepted_(config_->in_module_config_,
                               {load_shed_point_name.data(), load_shed_point_name.size()});
  }
}

void DynamicModuleResourceMonitorConfigScheduler::reportResourceUsage(double resource_pressure) {
  post([resource_pressure](DynamicModuleResourceMonitorConfig& config) {
    config.reportResourceUsage(resource_pressure);
  });
}

void DynamicModuleResourceMonitorConfigScheduler::reportFailure() {
  post([](DynamicModuleResourceMonitorConfig& config) { config.reportFailure(); });
}

void DynamicModuleResourceMonitorConfigScheduler::post(
    absl::AnyInvocable<void(DynamicModuleResourceMonitorConfig&)> report) {
  absl::MutexLock lock(&target_->mutex_);
  if (target_->main_thread_dispatcher_ == nullptr) {
    return;
  }
  target_->main_thread_dispatcher_->post(
      [config = target_->config_, report = std::move(report)]() mutable {
        if (const auto locked_config = config.lock(); locked_config != nullptr) {
          report(*locked_config);
        }
      });
}

absl::StatusOr<DynamicModuleResourceMonitorConfigSharedPtr>
newDynamicModuleResourceMonitorConfig(absl::string_view resource_monitor_name,
                                      absl::string_view resource_monitor_config,
                                      Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                      Event::Dispatcher& main_thread_dispatcher) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  auto on_config_new = dynamic_module->getFunctionPointer<OnResourceMonitorConfigNewType>(
      "envoy_dynamic_module_on_resource_monitor_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnResourceMonitorConfigDestroyType>(
      "envoy_dynamic_module_on_resource_monitor_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  // Which of these are required depends on whether the module enables synchronous feedback, which
  // is only known once it has created its configuration.
  auto on_update_resource_usage =
      dynamic_module->getFunctionPointer<OnResourceMonitorUpdateResourceUsageType>(
          "envoy_dynamic_module_on_resource_monitor_update_resource_usage");
  auto on_get_resource_usage =
      dynamic_module->getFunctionPointer<OnResourceMonitorGetResourceUsageType>(
          "envoy_dynamic_module_on_resource_monitor_get_resource_usage");
  auto on_load_accepted = dynamic_module->getFunctionPointer<OnResourceMonitorLoadAcceptedType>(
      "envoy_dynamic_module_on_resource_monitor_load_accepted");

  auto config = std::make_shared<DynamicModuleResourceMonitorConfig>(std::move(dynamic_module),
                                                                     main_thread_dispatcher);
  config->scheduler_target_->config_ = config;
  config->on_config_destroy_ = on_config_destroy.value();

  // The buffers borrow the caller's strings, which the ABI only promises for the duration of the
  // event hook.
  const envoy_dynamic_module_type_envoy_buffer name_buffer = {resource_monitor_name.data(),
                                                              resource_monitor_name.size()};
  const envoy_dynamic_module_type_envoy_buffer config_buffer = {resource_monitor_config.data(),
                                                                resource_monitor_config.size()};
  config->in_config_new_ = true;
  config->in_module_config_ =
      on_config_new.value()(static_cast<void*>(config.get()), name_buffer, config_buffer);
  config->in_config_new_ = false;
  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to initialize dynamic module resource monitor config");
  }
  if (config->synchronous_feedback_enabled_) {
    RETURN_IF_NOT_OK_REF(on_get_resource_usage.status());
    config->on_get_resource_usage_ = on_get_resource_usage.value();
    if (on_load_accepted.ok()) {
      config->on_load_accepted_ = on_load_accepted.value();
    }
  } else {
    RETURN_IF_NOT_OK_REF(on_update_resource_usage.status());
    config->on_update_resource_usage_ = on_update_resource_usage.value();
  }
  return config;
}

} // namespace DynamicModules
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
