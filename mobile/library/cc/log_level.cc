#include "log_level.h"

#include <stdexcept>

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"

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

  IS_ENVOY_BUG("unknown log level, defaulting to off");
  return LOG_LEVEL_LOOKUP[ARRAY_SIZE(LOG_LEVEL_LOOKUP) - 1].second;
}

LogLevel logLevelFromString(const std::string& str) {
  for (const auto& pair : LOG_LEVEL_LOOKUP) {
    if (pair.second == str) {
      return pair.first;
    }
  }

  IS_ENVOY_BUG("unknown log level, defaulting to off");
  return LOG_LEVEL_LOOKUP[ARRAY_SIZE(LOG_LEVEL_LOOKUP) - 1].first;
}

} // namespace Platform
} // namespace Envoy
