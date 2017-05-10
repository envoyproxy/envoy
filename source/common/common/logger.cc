#include "common/common/logger.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "envoy/thread/thread.h"

#include "spdlog/spdlog.h"

namespace Lyft {
namespace Logger {

#define GENERATE_LOGGER(X) Logger(#X),

std::vector<Logger> Registry::all_loggers_ = {ALL_LOGGER_IDS(GENERATE_LOGGER)};

Logger::Logger(const std::string& name) {
  logger_ = std::make_shared<spdlog::logger>(name, Registry::getSink());
  logger_->set_pattern("[%Y-%m-%d %T.%e][%t][%l][%n] %v");
  logger_->set_level(spdlog::level::trace);
}

void LockingStderrSink::log(const spdlog::details::log_msg& msg) {
  Thread::OptionalLockGuard<Thread::BasicLockable> guard(lock_);
  std::cerr << msg.formatted.str();
}

void LockingStderrSink::flush() {
  Thread::OptionalLockGuard<Thread::BasicLockable> guard(lock_);
  std::cerr << std::flush;
}

spdlog::logger& Registry::getLog(Id id) { return *all_loggers_[static_cast<int>(id)].logger_; }

void Registry::initialize(uint64_t log_level, Thread::BasicLockable& lock) {
  getSink()->setLock(lock);
  for (Logger& logger : all_loggers_) {
    logger.logger_->set_level(static_cast<spdlog::level::level_enum>(log_level));
  }
}

} // Logger
} // Lyft