// NOLINT(namespace-envoy)
#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/abi.h"

// Provide host-side implementations for ABI callbacks that are shared across
// all dynamic modules. This file is linked into dynamic_modules_lib so that
// core callbacks are always available, regardless of which extension point
// is being used (HTTP/Network/Listener/UDP/Bootstrap/etc).

extern "C" {

void envoy_dynamic_module_callback_bootstrap_extension_log(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr /*extension_envoy_ptr*/,
    envoy_dynamic_module_type_log_level level, envoy_dynamic_module_type_module_buffer message) {
  absl::string_view message_view(message.ptr, message.length);
  spdlog::logger& logger = Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules);

  switch (level) {
  case envoy_dynamic_module_type_log_level_Trace:
    ENVOY_LOG_TO_LOGGER(logger, trace, "{}", message_view);
    break;
  case envoy_dynamic_module_type_log_level_Debug:
    ENVOY_LOG_TO_LOGGER(logger, debug, "{}", message_view);
    break;
  case envoy_dynamic_module_type_log_level_Info:
    ENVOY_LOG_TO_LOGGER(logger, info, "{}", message_view);
    break;
  case envoy_dynamic_module_type_log_level_Warn:
    ENVOY_LOG_TO_LOGGER(logger, warn, "{}", message_view);
    break;
  case envoy_dynamic_module_type_log_level_Error:
    ENVOY_LOG_TO_LOGGER(logger, error, "{}", message_view);
    break;
  case envoy_dynamic_module_type_log_level_Critical:
    ENVOY_LOG_TO_LOGGER(logger, critical, "{}", message_view);
    break;
  default:
    break;
  }
}

} // extern "C"
