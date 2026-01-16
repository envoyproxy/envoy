// NOLINT(namespace-envoy)

// This file provides host-side implementations for ABI callbacks that are shared across
// all dynamic modules. These are the "Common Callbacks" declared in abi.h and are available
// regardless of which extension point is being used (HTTP/Network/Listener/UDP/Bootstrap/etc).

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/abi.h"

extern "C" {

bool envoy_dynamic_module_callback_log_enabled(envoy_dynamic_module_type_log_level level) {
  return Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules).level() <=
         static_cast<spdlog::level::level_enum>(level);
}

void envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level level,
                                       envoy_dynamic_module_type_module_buffer message) {
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

// ---------------------- Bootstrap extension scheduler callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the bootstrap extension abi_impl.cc when the bootstrap extension is used.
// This is necessary because the Rust SDK generates bindings for all callbacks in abi.h, and
// these symbols must be resolvable when any Rust module is loaded.
//
// We use IS_ENVOY_BUG instead of PANIC to allow coverage collection in tests. In non-coverage
// debug builds, IS_ENVOY_BUG will abort; in coverage builds it logs and continues, allowing the
// test to verify the error path was hit.

__attribute__((weak)) envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr
envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new: "
               "not implemented in this context");
  return nullptr;
}

__attribute__((weak)) void
envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(
    envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete: "
               "not implemented in this context");
}

__attribute__((weak)) void
envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(
    envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit: "
               "not implemented in this context");
}

} // extern "C"
