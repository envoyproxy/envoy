#include "source/extensions/tracers/datadog/logger.h"

#include <datadog/error.h>

#include <sstream>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

Logger::Logger(spdlog::logger& logger) : logger_(&logger) {}

// dd::Logger

void Logger::log_error(const LogFunc& write) {
  if (!ENVOY_LOG_COMP_LEVEL((*logger_), error)) {
    return;
  }

  std::ostringstream stream;
  write(stream);
  ENVOY_LOG_TO_LOGGER((*logger_), error, "{}", stream.str());
}

void Logger::log_startup(const LogFunc& write) {
  if (!ENVOY_LOG_COMP_LEVEL((*logger_), info)) {
    return;
  }

  std::ostringstream stream;
  write(stream);
  ENVOY_LOG_TO_LOGGER((*logger_), info, "{}", stream.str());
}

void Logger::log_error(const dd::Error& error) {
  ENVOY_LOG_TO_LOGGER((*logger_), error, "Datadog [error {}]: {}", int(error.code), error.message);
}

void Logger::log_error(dd::StringView message) {
  ENVOY_LOG_TO_LOGGER((*logger_), error, "{}", message);
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
