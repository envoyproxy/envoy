#include "library/common/common/logger_delegate.h"

#include <iostream>

#include "library/common/bridge/utility.h"
#include "library/common/data/utility.h"

namespace Envoy {
namespace Logger {

namespace {

envoy_log_level toEnvoyLogLevel(spdlog::level::level_enum spd_log_level) {
  switch (spd_log_level) {
  case spdlog::level::trace:
    return envoy_log_level::ENVOY_LOG_LEVEL_TRACE;
  case spdlog::level::debug:
    return envoy_log_level::ENVOY_LOG_LEVEL_DEBUG;
  case spdlog::level::info:
    return envoy_log_level::ENVOY_LOG_LEVEL_INFO;
  case spdlog::level::warn:
    return envoy_log_level::ENVOY_LOG_LEVEL_WARN;
  case spdlog::level::err:
    return envoy_log_level::ENVOY_LOG_LEVEL_ERROR;
  case spdlog::level::critical:
    return envoy_log_level::ENVOY_LOG_LEVEL_CRITICAL;
  case spdlog::level::off:
    return envoy_log_level::ENVOY_LOG_LEVEL_OFF;
  default:
    PANIC("not implemented");
  }
}

} //  namespace

void EventTrackingDelegate::logWithStableName(absl::string_view stable_name, absl::string_view,
                                              absl::string_view, absl::string_view msg) {
  if (event_tracker_.track == nullptr) {
    return;
  }

  event_tracker_.track(Bridge::Utility::makeEnvoyMap({{"name", "event_log"},
                                                      {"log_name", std::string(stable_name)},
                                                      {"message", std::string(msg)}}),
                       event_tracker_.context);
}

LambdaDelegate::LambdaDelegate(envoy_logger logger, DelegatingLogSinkSharedPtr log_sink)
    : EventTrackingDelegate(log_sink), logger_(logger) {
  setDelegate();
}

LambdaDelegate::~LambdaDelegate() {
  restoreDelegate();
  logger_.release(logger_.context);
}

void LambdaDelegate::log(absl::string_view msg, const spdlog::details::log_msg& log_msg) {
  logger_.log(toEnvoyLogLevel(log_msg.level), Data::Utility::copyToBridgeData(msg),
              logger_.context);
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
