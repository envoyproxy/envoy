#include "common/common/logger.h"

#include <cassert> // use direct system-assert to avoid cyclic dependency.
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "common/common/lock_guard.h"

#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/strip.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Logger {

StandardLogger::StandardLogger(const std::string& name)
    : Logger(std::make_shared<spdlog::logger>(name, Registry::getSink())) {}

SinkDelegate::SinkDelegate(DelegatingLogSinkSharedPtr log_sink)
    : previous_delegate_(log_sink->delegate()), log_sink_(log_sink) {
  log_sink->setDelegate(this);
}

SinkDelegate::~SinkDelegate() {
  assert(log_sink_->delegate() == this); // Ensures stacked allocation of delegates.
  log_sink_->setDelegate(previous_delegate_);
}

StderrSinkDelegate::StderrSinkDelegate(DelegatingLogSinkSharedPtr log_sink)
    : SinkDelegate(log_sink) {}

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

  // This memory buffer must exist in the scope of the entire function,
  // otherwise the string_view will refer to memory that is already free.
  spdlog::memory_buf_t formatted;
  if (formatter_) {
    formatter_->format(msg, formatted);
    msg_view = absl::string_view(formatted.data(), formatted.size());
  }
  lock.Release();

  if (should_escape_) {
    sink_->log(escapeLogLine(msg_view));
  } else {
    sink_->log(msg_view);
  }
}

std::string DelegatingLogSink::escapeLogLine(absl::string_view msg_view) {
  // Split the actual message from the trailing whitespace.
  auto eol_it = std::find_if_not(msg_view.rbegin(), msg_view.rend(), absl::ascii_isspace);
  absl::string_view msg_leading = msg_view.substr(0, msg_view.rend() - eol_it);
  absl::string_view msg_trailing_whitespace =
      msg_view.substr(msg_view.rend() - eol_it, eol_it - msg_view.rbegin());

  // Escape the message, but keep the whitespace unescaped.
  return absl::StrCat(absl::CEscape(msg_leading), msg_trailing_whitespace);
}

DelegatingLogSinkSharedPtr DelegatingLogSink::init() {
  DelegatingLogSinkSharedPtr delegating_sink(new DelegatingLogSink);
  delegating_sink->stderr_sink_ = std::make_unique<StderrSinkDelegate>(delegating_sink);
  return delegating_sink;
}

static Context* current_context = nullptr;

Context::Context(spdlog::level::level_enum log_level, const std::string& log_format,
                 Thread::BasicLockable& lock, bool should_escape)
    : log_level_(log_level), log_format_(log_format), lock_(lock), should_escape_(should_escape),
      save_context_(current_context) {
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
  Registry::getSink()->setShouldEscape(should_escape_);
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
