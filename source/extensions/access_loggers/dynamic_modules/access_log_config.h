#pragma once

#include "envoy/extensions/access_loggers/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/common/statusor.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace DynamicModules {

// Type aliases for function pointers resolved from the module.
using OnAccessLoggerConfigNewType = decltype(&envoy_dynamic_module_on_access_logger_config_new);
using OnAccessLoggerConfigDestroyType =
    decltype(&envoy_dynamic_module_on_access_logger_config_destroy);
using OnAccessLoggerNewType = decltype(&envoy_dynamic_module_on_access_logger_new);
using OnAccessLoggerLogType = decltype(&envoy_dynamic_module_on_access_logger_log);
using OnAccessLoggerDestroyType = decltype(&envoy_dynamic_module_on_access_logger_destroy);
using OnAccessLoggerFlushType = decltype(&envoy_dynamic_module_on_access_logger_flush);

/**
 * Configuration for dynamic module access loggers. This resolves and holds the symbols used for
 * access logging. Multiple access log instances may share this config.
 *
 * Note: Symbol resolution and in-module config creation are done in the factory function
 * newDynamicModuleAccessLogConfig() to provide graceful error handling. The constructor
 * only initializes basic members.
 */
class DynamicModuleAccessLogConfig {
public:
  /**
   * Constructor for the config. Symbol resolution is done in newDynamicModuleAccessLogConfig().
   * @param logger_name the name of the logger.
   * @param logger_config the configuration bytes for the logger.
   * @param dynamic_module the dynamic module to use.
   */
  DynamicModuleAccessLogConfig(const absl::string_view logger_name,
                               const absl::string_view logger_config,
                               Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleAccessLogConfig();

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_access_logger_config_module_ptr in_module_config_{nullptr};

  // The function pointers for the module related to the access logger. All required ones are
  // resolved during newDynamicModuleAccessLogConfig() and guaranteed non-nullptr after that.
  OnAccessLoggerConfigDestroyType on_config_destroy_{nullptr};
  OnAccessLoggerNewType on_logger_new_{nullptr};
  OnAccessLoggerLogType on_logger_log_{nullptr};
  OnAccessLoggerDestroyType on_logger_destroy_{nullptr};
  // Optional flush callback. Called before logger destruction during shutdown.
  OnAccessLoggerFlushType on_logger_flush_{nullptr};

private:
  // Allow the factory function to access private members for initialization.
  friend absl::StatusOr<std::shared_ptr<DynamicModuleAccessLogConfig>>
  newDynamicModuleAccessLogConfig(const absl::string_view logger_name,
                                  const absl::string_view logger_config,
                                  Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  // The name of the logger passed in the constructor.
  const std::string logger_name_;

  // The configuration bytes for the logger.
  const std::string logger_config_;

  // The handle for the module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleAccessLogConfigSharedPtr = std::shared_ptr<DynamicModuleAccessLogConfig>;

/**
 * Creates a new DynamicModuleAccessLogConfig for the given configuration.
 * @param logger_name the name of the logger.
 * @param logger_config the configuration bytes for the logger.
 * @param dynamic_module the dynamic module to use.
 * @return a shared pointer to the new config object or an error if symbol resolution failed.
 */
absl::StatusOr<DynamicModuleAccessLogConfigSharedPtr>
newDynamicModuleAccessLogConfig(const absl::string_view logger_name,
                                const absl::string_view logger_config,
                                Extensions::DynamicModules::DynamicModulePtr dynamic_module);

} // namespace DynamicModules
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
