// NOLINT(namespace-envoy)
#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/abi.h"

// Provide host-side implementations for ABI callbacks that are shared across
// all dynamic modules. This file is linked into dynamic_modules_lib so that
// core callbacks are always available, regardless of which extension point
// is being used (HTTP/Network/Listener/UDP/Bootstrap/etc).

extern "C" {

void envoy_dynamic_module_callback_bootstrap_extension_log(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr /*extension_envoy_ptr*/, uint32_t level,
    const char* message_ptr, size_t message_size) {
  absl::string_view message(message_ptr, message_size);

  switch (level) {
  case 0:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), trace,
                        "{}", message);
    break;
  case 1:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), debug,
                        "{}", message);
    break;
  case 2:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), info,
                        "{}", message);
    break;
  case 3:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
                        "{}", message);
    break;
  case 4:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), error,
                        "{}", message);
    break;
  case 5:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules),
                        critical, "{}", message);
    break;
  default:
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), info,
                        "{}", message);
    break;
  }
}

} // extern "C"
