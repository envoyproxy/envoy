#include "library/common/logger/logger_delegate.h"

#include <iostream>

#include "library/common/bridge/utility.h"

namespace Envoy {
namespace Logger {

void EventTrackingDelegate::logWithStableName(absl::string_view stable_name, absl::string_view,
                                              absl::string_view, absl::string_view msg) {
  if (event_tracker_ == nullptr || (*event_tracker_) == nullptr) {
    return;
  }

  (*event_tracker_)
      ->on_track_({{"name", "event_log"},
                   {"log_name", std::string(stable_name)},
                   {"message", std::string(msg)}});
}

LambdaDelegate::LambdaDelegate(std::unique_ptr<EnvoyLogger> logger,
                               DelegatingLogSinkSharedPtr log_sink)
    : EventTrackingDelegate(log_sink), logger_(std::move(logger)) {
  setDelegate();
}

LambdaDelegate::~LambdaDelegate() {
  restoreDelegate();
  logger_->on_exit_();
}

void LambdaDelegate::log(absl::string_view msg, const spdlog::details::log_msg& log_msg) {
  // Logger::Levels is simply an alias to spdlog::level::level_enum, so we can safely cast it.
  logger_->on_log_(static_cast<Logger::Levels>(log_msg.level), std::string(msg));
}

DefaultDelegate::DefaultDelegate(absl::Mutex& mutex, DelegatingLogSinkSharedPtr log_sink)
    : EventTrackingDelegate(log_sink), mutex_(mutex) {
  setDelegate();
}

DefaultDelegate::~DefaultDelegate() { restoreDelegate(); }

// SinkDelegate
void DefaultDelegate::log(absl::string_view msg, const spdlog::details::log_msg&) {
  absl::MutexLock l(&mutex_);
  std::cerr << msg;
}

void DefaultDelegate::flush() {
  absl::MutexLock l(&mutex_);
  std::cerr << std::flush;
}

} // namespace Logger
} // namespace Envoy
