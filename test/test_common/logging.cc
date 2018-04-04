#include "test/test_common/logging.h"

#include "common/common/assert.h"

namespace Envoy {

LogLevelSetter::LogLevelSetter(spdlog::level::level_enum log_level) {
  for (Logger::Logger& logger : Logger::Registry::loggers()) {
    previous_levels_.push_back(logger.level());
    logger.setLevel(log_level);
  }
}

LogLevelSetter::~LogLevelSetter() {
  auto prev_level = previous_levels_.begin();
  for (Logger::Logger& logger : Logger::Registry::loggers()) {
    ASSERT(prev_level != previous_levels_.end());
    logger.setLevel(*prev_level);
    ++prev_level;
  }
  ASSERT(prev_level == previous_levels_.end());
}

LogRecordingSink::LogRecordingSink(Logger::DelegatingLogSinkPtr log_sink)
    : Logger::SinkDelegate(log_sink) {}
LogRecordingSink::~LogRecordingSink() {}

void LogRecordingSink::log(absl::string_view msg) {
  previous_delegate()->log(msg);
  messages_.push_back(std::string(msg));
}

void LogRecordingSink::flush() { previous_delegate()->flush(); }

} // namespace Envoy
