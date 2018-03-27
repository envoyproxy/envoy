#include "common/common/logger.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "envoy/thread/thread.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Logger {

#define GENERATE_LOGGER(X) Logger(#X),

Logger::Logger(const std::string& name) {
  logger_ = std::make_shared<spdlog::logger>(name, Registry::getSink());
  logger_->set_pattern("[%Y-%m-%d %T.%e][%t][%l][%n] %v");
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

FileSinkDelegate::FileSinkDelegate(const std::string& log_path,
                                   AccessLog::AccessLogManager& log_manager,
                                   DelegatingLogSinkPtr log_sink)
    : SinkDelegate(log_sink), log_file_(log_manager.createAccessLog(log_path)) {}

void FileSinkDelegate::log(absl::string_view msg) {
  // Logfiles have internal locking to ensure serial, non-interleaved
  // writes, so no additional locking needed here.
  //
  // TODO(jmarantz): filesystem file write should take string_view. Adding that requires
  // finding a way to make that work for mocked filesystem writes.
  log_file_->write(std::string(msg));
}

void FileSinkDelegate::flush() {
  // Logfiles have internal locking to ensure serial, non-interleaved
  // writes, so no additional locking needed here.
  log_file_->flush();
}

StderrSinkDelegate::StderrSinkDelegate(DelegatingLogSinkPtr log_sink) : SinkDelegate(log_sink) {}

void StderrSinkDelegate::log(absl::string_view msg) {
  Thread::OptionalLockGuard<Thread::BasicLockable> guard(lock_);
  std::cerr << msg;
}

void StderrSinkDelegate::flush() {
  Thread::OptionalLockGuard<Thread::BasicLockable> guard(lock_);
  std::cerr << std::flush;
}

void DelegatingLogSink::log(const spdlog::details::log_msg& msg) {
  sink_->log(msg.formatted.str());
}

DelegatingLogSinkPtr DelegatingLogSink::init() {
  DelegatingLogSinkPtr delegating_sink(new DelegatingLogSink);
  delegating_sink->stderr_sink_ = std::make_unique<StderrSinkDelegate>(delegating_sink);
  return delegating_sink;
}

std::vector<Logger>& Registry::allLoggers() {
  static std::vector<Logger>* all_loggers =
      new std::vector<Logger>({ALL_LOGGER_IDS(GENERATE_LOGGER)});
  return *all_loggers;
}

spdlog::logger& Registry::getLog(Id id) { return *allLoggers()[static_cast<int>(id)].logger_; }

void Registry::initialize(uint64_t log_level, Thread::BasicLockable& lock) {
  getSink()->setLock(lock);
  for (Logger& logger : allLoggers()) {
    logger.logger_->set_level(static_cast<spdlog::level::level_enum>(log_level));
  }
}

} // Logger
} // namespace Envoy
