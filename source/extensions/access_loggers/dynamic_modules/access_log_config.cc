#include "source/extensions/access_loggers/dynamic_modules/access_log_config.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace DynamicModules {

DynamicModuleAccessLogConfig::DynamicModuleAccessLogConfig(
    const absl::string_view logger_name, const absl::string_view logger_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope)
    : stats_scope_(stats_scope.createScope(std::string(AccessLogStatsNamespace) + ".")),
      stat_name_pool_(stats_scope_->symbolTable()), logger_name_(logger_name),
      logger_config_(logger_config), dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleAccessLogConfig::~DynamicModuleAccessLogConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

absl::StatusOr<DynamicModuleAccessLogConfigSharedPtr> newDynamicModuleAccessLogConfig(
    const absl::string_view logger_name, const absl::string_view logger_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  // Resolve the symbols for the access logger using graceful error handling.
  auto on_config_new = dynamic_module->getFunctionPointer<OnAccessLoggerConfigNewType>(
      "envoy_dynamic_module_on_access_logger_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnAccessLoggerConfigDestroyType>(
      "envoy_dynamic_module_on_access_logger_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_logger_new = dynamic_module->getFunctionPointer<OnAccessLoggerNewType>(
      "envoy_dynamic_module_on_access_logger_new");
  RETURN_IF_NOT_OK_REF(on_logger_new.status());

  auto on_logger_log = dynamic_module->getFunctionPointer<OnAccessLoggerLogType>(
      "envoy_dynamic_module_on_access_logger_log");
  RETURN_IF_NOT_OK_REF(on_logger_log.status());

  auto on_logger_destroy = dynamic_module->getFunctionPointer<OnAccessLoggerDestroyType>(
      "envoy_dynamic_module_on_access_logger_destroy");
  RETURN_IF_NOT_OK_REF(on_logger_destroy.status());

  // Flush is optional - module may not implement it.
  auto on_logger_flush = dynamic_module->getFunctionPointer<OnAccessLoggerFlushType>(
      "envoy_dynamic_module_on_access_logger_flush");

  auto config = std::make_shared<DynamicModuleAccessLogConfig>(
      logger_name, logger_config, std::move(dynamic_module), stats_scope);

  // Store the resolved function pointers.
  config->on_config_destroy_ = on_config_destroy.value();
  config->on_logger_new_ = on_logger_new.value();
  config->on_logger_log_ = on_logger_log.value();
  config->on_logger_destroy_ = on_logger_destroy.value();
  config->on_logger_flush_ = on_logger_flush.ok() ? on_logger_flush.value() : nullptr;

  // Create the in-module configuration.
  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = config->logger_name_.data(),
                                                     .length = config->logger_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buf = {.ptr = config->logger_config_.data(),
                                                       .length = config->logger_config_.size()};
  config->in_module_config_ =
      (*on_config_new.value())(static_cast<void*>(config.get()), name_buf, config_buf);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module access logger config");
  }
  return config;
}

} // namespace DynamicModules
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
