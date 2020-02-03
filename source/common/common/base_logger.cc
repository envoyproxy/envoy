#include "common/common/base_logger.h"

namespace Envoy {
namespace Logger {

const char* Logger::DEFAULT_LOG_FORMAT = "[%Y-%m-%d %T.%e][%t][%l][%n] %v";

Logger::Logger(std::shared_ptr<spdlog::logger> logger) : logger_(logger) {
  logger_->set_pattern(DEFAULT_LOG_FORMAT);
  logger_->set_level(spdlog::level::trace);

  // Ensure that critical errors, especially ASSERT/PANIC, get flushed
  logger_->flush_on(spdlog::level::critical);
}

} // namespace Logger
} // namespace Envoy
