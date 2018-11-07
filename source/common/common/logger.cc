#include "common/common/logger.h"

#include <cassert> // use direct system-assert to avoid cyclic dependency.
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "common/common/lock_guard.h"

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

SinkDelegate::SinkDelegate(DelegatingLogSinkPtr log_sink)
    : previous_delegate_(log_sink->delegate()), log_sink_(log_sink) {
  log_sink->setDelegate(this);
}

SinkDelegate::~SinkDelegate() {
  assert(log_sink_->delegate() == this); // Ensures stacked allocation of delegates.
  log_sink_->setDelegate(previous_delegate_);
}

StderrSinkDelegate::StderrSinkDelegate(DelegatingLogSinkPtr log_sink) : SinkDelegate(log_sink) {}

void StderrSinkDelegate::log(absl::string_view msg) {
  Thread::OptionalLockGuard guard(lock_);
  std::cerr << msg;
}

void StderrSinkDelegate::flush() {
  Thread::OptionalLockGuard guard(lock_);
  std::cerr << std::flush;
}

void DelegatingLogSink::set_formatter(std::unique_ptr<spdlog::formatter> formatter) {
  absl::MutexLock lock(&format_mutex_);
  formatter_ = std::move(formatter);
}

void DelegatingLogSink::log(const spdlog::details::log_msg& msg) {
  absl::ReleasableMutexLock lock(&format_mutex_);
  if (!formatter_) {
    lock.Release();
    sink_->log(fmt::to_string(msg.raw));
    return;
  }

  fmt::memory_buffer formatted;
  formatter_->format(msg, formatted);
  lock.Release();
  sink_->log(fmt::to_string(formatted));
}

DelegatingLogSinkPtr DelegatingLogSink::init() {
  DelegatingLogSinkPtr delegating_sink(new DelegatingLogSink);
  delegating_sink->stderr_sink_ = std::make_unique<StderrSinkDelegate>(delegating_sink);
  return delegating_sink;
}

static Context* current_context = nullptr;

Context::Context(spdlog::level::level_enum log_level, const std::string& log_format,
                 Thread::BasicLockable& lock)
    : log_level_(log_level), log_format_(log_format), lock_(lock), save_context_(current_context) {
  current_context = this;
  activate();
}

Context::~Context() {
  current_context = save_context_;
  if (current_context != nullptr) {
    current_context->activate();
  } else {
    Registry::getSink()->clearLock();
  }
}

void Context::activate() {
  Registry::getSink()->setLock(lock_);
  Registry::setLogLevel(log_level_);
  Registry::setLogFormat(log_format_);
}

std::vector<Logger>& Registry::allLoggers() {
  static std::vector<Logger>* all_loggers =
      new std::vector<Logger>({ALL_LOGGER_IDS(GENERATE_LOGGER)});
  return *all_loggers;
}

spdlog::logger& Registry::getLog(Id id) { return *allLoggers()[static_cast<int>(id)].logger_; }

void Registry::setLogLevel(spdlog::level::level_enum log_level) {
  for (Logger& logger : allLoggers()) {
    logger.logger_->set_level(static_cast<spdlog::level::level_enum>(log_level));
  }
}

void Registry::setLogFormat(const std::string& log_format) {
  for (Logger& logger : allLoggers()) {
    logger.logger_->set_pattern(log_format);
  }
}

Logger* Registry::logger(const std::string& log_name) {
  Logger* logger_to_return = nullptr;
  for (Logger& logger : loggers()) {
    if (logger.name() == log_name) {
      logger_to_return = &logger;
      break;
    }
  }
  return logger_to_return;
}

} // namespace Logger
} // namespace Envoy
