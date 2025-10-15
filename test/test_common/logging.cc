#include "test/test_common/logging.h"

#include "source/common/common/assert.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {

LogLevelSetter::LogLevelSetter(spdlog::level::level_enum log_level) {
  if (Logger::Context::useFineGrainLogger()) {
    previous_fine_grain_levels_ = getFineGrainLogContext().getAllFineGrainLogLevelsForTest();
    getFineGrainLogContext().setAllFineGrainLoggers(log_level);
  } else {
    for (Logger::Logger& logger : Logger::Registry::loggers()) {
      previous_levels_.push_back(logger.level());
      logger.setLevel(log_level);
    }
  }
}

LogLevelSetter::~LogLevelSetter() {
  if (Logger::Context::useFineGrainLogger()) {
    for (const auto& it : previous_fine_grain_levels_) {
      getFineGrainLogContext().setFineGrainLogger(it.first, it.second);
    }
  } else {
    auto prev_level = previous_levels_.begin();
    for (Logger::Logger& logger : Logger::Registry::loggers()) {
      ASSERT(prev_level != previous_levels_.end());
      logger.setLevel(*prev_level);
      ++prev_level;
    }
    ASSERT(prev_level == previous_levels_.end());
  }
}

LogExpectation::LogExpectation(
    LogRecordingSink& sink,
    absl::AnyInvocable<void(Logger::Logger::Levels, const std::string&)> on_log)
    : sink_(sink), on_log_(std::move(on_log)) {
  sink_.addExpectation(this);
}
LogExpectation::~LogExpectation() { sink_.removeExpectation(this); }

LogRecordingSink::LogRecordingSink(Logger::DelegatingLogSinkSharedPtr log_sink)
    : Logger::SinkDelegate(log_sink) {
  setDelegate();
}

LogRecordingSink::~LogRecordingSink() { restoreDelegate(); }

void LogRecordingSink::log(absl::string_view msg, const spdlog::details::log_msg& log_msg) {
  previousDelegate()->log(msg, log_msg);

  if (enabled_) {
    absl::MutexLock ml(&mtx_);
    messages_.push_back(std::string(msg));
  }

  absl::MutexLock ml(&exp_mtx_);
  for (auto* expect : expectations_) {
    expect->on_log_(static_cast<Logger::Logger::Levels>(log_msg.level), std::string(msg));
  }
}

const std::vector<std::string> LogRecordingSink::messages() const {
  absl::MutexLock ml(&mtx_);
  std::vector<std::string> copy(messages_);
  return copy;
}

void LogRecordingSink::start() {
  ASSERT(!enabled_);
  enabled_ = true;
}

void LogRecordingSink::stop() {
  ASSERT(enabled_);
  enabled_ = false;
  absl::MutexLock ml(&mtx_);
  messages_.clear();
}

void LogRecordingSink::addExpectation(LogExpectation* exp) {
  absl::MutexLock ml(&exp_mtx_);
  expectations_.insert(exp);
}

void LogRecordingSink::removeExpectation(LogExpectation* exp) {
  absl::MutexLock ml(&exp_mtx_);
  expectations_.erase(exp);
}

void LogRecordingSink::flush() { previousDelegate()->flush(); }

} // namespace Envoy
