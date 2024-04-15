#pragma once

#include <string>

#include "source/common/common/base_logger.h"

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"

namespace Envoy {

/** The callbacks for the Envoy Engine. */
struct EngineCallbacks {
  absl::AnyInvocable<void()> on_engine_running_ = [] {};
  absl::AnyInvocable<void()> on_exit_ = [] {};
};

/** The callbacks for Envoy Logger. */
struct EnvoyLogger {
  absl::AnyInvocable<void(Logger::Logger::Levels, const std::string&)> on_log_ =
      [](Logger::Logger::Levels, const std::string&) {};
  absl::AnyInvocable<void()> on_exit_ = [] {};
};

inline constexpr absl::string_view ENVOY_EVENT_TRACKER_API_NAME = "event_tracker_api";

/** The callbacks for Envoy Event Tracker. */
struct EnvoyEventTracker {
  absl::AnyInvocable<void(const absl::flat_hash_map<std::string, std::string>&)> on_track_ =
      [](const absl::flat_hash_map<std::string, std::string>&) {};
  absl::AnyInvocable<void()> on_exit_ = [] {};
};

} // namespace Envoy
