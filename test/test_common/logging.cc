#include "test/test_common/logging.h"

#include "common/common/assert.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {

LogLevelSetter::LogLevelSetter(spdlog::level::level_enum log_level) {
  if (Logger::Context::useFancyLogger()) {
    previous_fancy_levels_ = getFancyContext().getAllFancyLogLevelsForTest();
    getFancyContext().setAllFancyLoggers(log_level);
  } else {
    for (Logger::Logger& logger : Logger::Registry::loggers()) {
      previous_levels_.push_back(logger.level());
      logger.setLevel(log_level);
    }
  }
}

LogLevelSetter::~LogLevelSetter() {
  if (Logger::Context::useFancyLogger()) {
    for (const auto& it : previous_fancy_levels_) {
      getFancyContext().setFancyLogger(it.first, it.second);
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

LogRecordingSink::LogRecordingSink(Logger::DelegatingLogSinkSharedPtr log_sink)
    : Logger::SinkDelegate(log_sink) {
  setDelegate();
}

LogRecordingSink::~LogRecordingSink() { restoreDelegate(); }

void LogRecordingSink::log(absl::string_view msg) {
  previousDelegate()->log(msg);

  absl::MutexLock ml(&mtx_);
  messages_.push_back(std::string(msg));
}

void LogRecordingSink::flush() { previousDelegate()->flush(); }

} // namespace Envoy
