#include "common/common/logger.h"

#include <cassert> // use direct system-assert to avoid cyclic dependency.
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "common/common/lock_guard.h"

#include "spdlog/spdlog.h"
#include "absl/strings/escaping.h"

namespace Envoy {
namespace Logger {

#define GENERATE_LOGGER(X) Logger(#X),

const char* Logger::DEFAULT_LOG_FORMAT = "[%Y-%m-%d %T.%e][%t][%l][%n] %v";

// Ensure linker knows where to allocate the static sink
// https://stackoverflow.com/a/9110546/4402434
DelegatingLogSinkPtr Registry::sink_;

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
  absl::string_view msg_view = absl::string_view(msg.payload.data(), msg.payload.size());
  if (formatter_) {
    fmt::memory_buffer formatted;
    formatter_->format(msg, formatted);
    msg_view = absl::string_view(formatted.data(), formatted.size());
  }
  lock.Release();

  if (should_escape_newlines_) {
    sink_->log(absl::CEscape(msg_view));
    sink_->log("\r\n");
  } else {
    sink_->log(msg_view);
  }
}

DelegatingLogSinkPtr DelegatingLogSink::init(bool should_escape_newlines) {
  DelegatingLogSinkPtr delegating_sink(new DelegatingLogSink);

  delegating_sink->stderr_sink_ = std::make_unique<StderrSinkDelegate>(delegating_sink);
  delegating_sink->should_escape_newlines_ = should_escape_newlines;

  return delegating_sink;
}

static Context* current_context = nullptr;

Context::Context(spdlog::level::level_enum log_level, const std::string& log_format,
                 Thread::BasicLockable& lock)
    : Context(log_level, log_format, lock, false) {}

Context::Context(spdlog::level::level_enum log_level, const std::string& log_format,
                 Thread::BasicLockable& lock, bool should_escape_newlines)
    : log_level_(log_level), log_format_(log_format), lock_(lock),
      should_escape_newlines_(should_escape_newlines), save_context_(current_context) {
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
  Registry::initSink(should_escape_newlines_);
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
