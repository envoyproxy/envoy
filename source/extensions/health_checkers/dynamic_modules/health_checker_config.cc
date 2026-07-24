#include "source/extensions/health_checkers/dynamic_modules/health_checker_config.h"

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace DynamicModules {

DynamicModuleHealthCheckerConfig::DynamicModuleHealthCheckerConfig(
    absl::string_view health_checker_name, absl::string_view health_checker_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : health_checker_name_(health_checker_name), health_checker_config_(health_checker_config),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleHealthCheckerConfig::~DynamicModuleHealthCheckerConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

absl::StatusOr<DynamicModuleHealthCheckerConfigSharedPtr>
newDynamicModuleHealthCheckerConfig(absl::string_view health_checker_name,
                                    absl::string_view health_checker_config,
                                    Extensions::DynamicModules::DynamicModulePtr dynamic_module) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  // Resolve the symbols for the health checker using graceful error handling.
  auto on_config_new = dynamic_module->getFunctionPointer<OnHealthCheckerConfigNewType>(
      "envoy_dynamic_module_on_health_checker_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnHealthCheckerConfigDestroyType>(
      "envoy_dynamic_module_on_health_checker_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_session_new = dynamic_module->getFunctionPointer<OnHealthCheckerSessionNewType>(
      "envoy_dynamic_module_on_health_checker_session_new");
  RETURN_IF_NOT_OK_REF(on_session_new.status());

  auto on_session_on_interval =
      dynamic_module->getFunctionPointer<OnHealthCheckerSessionOnIntervalType>(
          "envoy_dynamic_module_on_health_checker_session_on_interval");
  RETURN_IF_NOT_OK_REF(on_session_on_interval.status());

  auto on_session_destroy = dynamic_module->getFunctionPointer<OnHealthCheckerSessionDestroyType>(
      "envoy_dynamic_module_on_health_checker_session_destroy");
  RETURN_IF_NOT_OK_REF(on_session_destroy.status());

  // Timeout is optional - module may not implement it.
  auto on_session_on_timeout =
      dynamic_module->getFunctionPointer<OnHealthCheckerSessionOnTimeoutType>(
          "envoy_dynamic_module_on_health_checker_session_on_timeout");

  auto config = std::make_shared<DynamicModuleHealthCheckerConfig>(
      health_checker_name, health_checker_config, std::move(dynamic_module));

  // Store the resolved function pointers.
  config->on_config_destroy_ = on_config_destroy.value();
  config->on_session_new_ = on_session_new.value();
  config->on_session_on_interval_ = on_session_on_interval.value();
  config->on_session_destroy_ = on_session_destroy.value();
  config->on_session_on_timeout_ =
      on_session_on_timeout.ok() ? on_session_on_timeout.value() : nullptr;

  // Create the in-module configuration.
  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = config->health_checker_name_.data(),
                                                     .length = config->health_checker_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buf = {.ptr = config->health_checker_config_.data(),
                                                       .length =
                                                           config->health_checker_config_.size()};
  config->in_module_config_ =
      (*on_config_new.value())(static_cast<void*>(config.get()), name_buf, config_buf);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module health checker config");
  }
  return config;
}

} // namespace DynamicModules
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
