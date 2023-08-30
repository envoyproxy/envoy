#pragma once

#include <string>

#include "source/common/common/base_logger.h"

namespace Envoy {
namespace Platform {

enum class LogLevel {
  trace = Envoy::Logger::Logger::Levels::trace,
  debug = Envoy::Logger::Logger::Levels::debug,
  info = Envoy::Logger::Logger::Levels::info,
  warn = Envoy::Logger::Logger::Levels::warn,
  error = Envoy::Logger::Logger::Levels::error,
  critical = Envoy::Logger::Logger::Levels::critical,
  off = Envoy::Logger::Logger::Levels::off,
};

std::string logLevelToString(LogLevel method);
LogLevel logLevelFromString(const std::string& str);

} // namespace Platform
} // namespace Envoy
