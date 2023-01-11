#pragma once

#include <string>

#include "source/common/common/base_logger.h"

namespace Envoy {
namespace Platform {

using LogLevel = Envoy::Logger::Logger::Levels;  // IWYU: export

std::string logLevelToString(LogLevel method);
LogLevel logLevelFromString(const std::string& str);

} // namespace Platform
} // namespace Envoy
