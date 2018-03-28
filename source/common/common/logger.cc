#include "common/common/logger.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "envoy/thread/thread.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Logger {

#define GENERATE_LOGGER(X) Logger(#X),

const char* Logger::DEFAULT_LOG_FORMAT = "[%Y-%m-%d %T.%e][%t][%l][%n] %v";

Logger::Logger(const std::string& name) {
  logger_ = std::make_shared<spdlog::logger>(name, Registry::getSink());
  logger_->set_pattern(DEFAULT_LOG_FORMAT);
  logger_->set_level(spdlog::level::trace);

  // Ensure that critical errors, especially ASSERT/PANIC, get flushed
  logger_->flush_on(spdlog::level::critical);
}

void LockingStderrOrFileSink::logToStdErr() { log_file_.reset(); }

void LockingStderrOrFileSink::logToFile(const std::string& log_path,
                                        AccessLog::AccessLogManager& log_manager) {
  log_file_ = log_manager.createAccessLog(log_path);
}

std::vector<Logger>& Registry::allLoggers() {
  static std::vector<Logger>* all_loggers =
      new std::vector<Logger>({ALL_LOGGER_IDS(GENERATE_LOGGER)});
  return *all_loggers;
}

void LockingStderrOrFileSink::log(const spdlog::details::log_msg& msg) {
  if (log_file_) {
    // Logfiles have internal locking to ensure serial, non-interleaved
    // writes, so no additional locking needed here.
    log_file_->write(msg.formatted.str());
  } else {
    Thread::OptionalLockGuard<Thread::BasicLockable> guard(lock_);
    std::cerr << msg.formatted.str();
  }
}

void LockingStderrOrFileSink::flush() {
  if (log_file_) {
    // Logfiles have internal locking to ensure serial, non-interleaved
    // writes, so no additional locking needed here.
    log_file_->flush();
  } else {
    Thread::OptionalLockGuard<Thread::BasicLockable> guard(lock_);
    std::cerr << std::flush;
  }
}

spdlog::logger& Registry::getLog(Id id) { return *allLoggers()[static_cast<int>(id)].logger_; }

void Registry::initialize(uint64_t log_level, const std::string& log_format,
                          Thread::BasicLockable& lock) {
  getSink()->setLock(lock);
  for (Logger& logger : allLoggers()) {
    logger.logger_->set_level(static_cast<spdlog::level::level_enum>(log_level));
    logger.logger_->set_pattern(log_format);
  }
}

} // Logger
} // namespace Envoy
