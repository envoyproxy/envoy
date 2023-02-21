#include "source/extensions/tracers/datadog/logger.h"

#include <sstream>

#include "datadog/error.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

Logger::Logger(spdlog::logger& logger) : logger_(logger) {}

// datadog::tracing::Logger

void Logger::log_error(const LogFunc& write) {
  if (!ENVOY_LOG_COMP_LEVEL(logger_, error)) {
    return;
  }

  std::ostringstream stream;
  write(stream);
  ENVOY_LOG_TO_LOGGER(logger_, error, "{}", stream.str());
}

void Logger::log_startup(const LogFunc& write) {
  if (!ENVOY_LOG_COMP_LEVEL(logger_, info)) {
    return;
  }

  std::ostringstream stream;
  write(stream);
  ENVOY_LOG_TO_LOGGER(logger_, info, "{}", stream.str());
}

void Logger::log_error(const datadog::tracing::Error& error) {
  ENVOY_LOG_TO_LOGGER(logger_, error, "Datadog [error {}]: {}", int(error.code), error.message);
}

void Logger::log_error(datadog::tracing::StringView message) {
  ENVOY_LOG_TO_LOGGER(logger_, error, "{}", message);
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
