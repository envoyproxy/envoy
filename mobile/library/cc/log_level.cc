#include "log_level.h"

#include <stdexcept>

namespace Envoy {
namespace Platform {

static const std::pair<LogLevel, std::string> LOG_LEVEL_LOOKUP[]{
    {LogLevel::trace, "trace"}, {LogLevel::debug, "debug"}, {LogLevel::info, "info"},
    {LogLevel::warn, "warn"},   {LogLevel::error, "error"}, {LogLevel::critical, "critical"},
    {LogLevel::off, "off"},
};

std::string logLevelToString(LogLevel method) {
  for (const auto& pair : LOG_LEVEL_LOOKUP) {
    if (pair.first == method) {
      return pair.second;
    }
  }

  throw std::out_of_range("unknown log level type");
}

LogLevel logLevelFromString(const std::string& str) {
  for (const auto& pair : LOG_LEVEL_LOOKUP) {
    if (pair.second == str) {
      return pair.first;
    }
  }

  throw std::out_of_range("unknown log level type");
}

} // namespace Platform
} // namespace Envoy
