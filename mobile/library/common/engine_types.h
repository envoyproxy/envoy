#pragma once

#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"

#include "source/common/common/base_logger.h"

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "library/common/types/c_types.h"

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

/** The Envoy error passed into `EnvoyStreamCallbacks::on_error_` callback. */
struct EnvoyError {
  envoy_error_code_t error_code;
  std::string message;
  absl::optional<int> attempt_count = absl::nullopt;
  absl::optional<std::exception> cause = absl::nullopt;
};

/** The callbacks for the stream. */
struct EnvoyStreamCallbacks {
  absl::AnyInvocable<void(const Http::ResponseHeaderMap&, bool /* end_stream */,
                          envoy_stream_intel)>
      on_headers_ = [](const Http::ResponseHeaderMap&, bool, envoy_stream_intel) {};

  absl::AnyInvocable<void(const Buffer::Instance& buffer, uint64_t /* length */,
                          bool /* end_stream */, envoy_stream_intel)>
      on_data_ = [](const Buffer::Instance&, uint64_t, bool, envoy_stream_intel) {};

  absl::AnyInvocable<void(const Http::ResponseTrailerMap&, envoy_stream_intel)> on_trailers_ =
      [](const Http::ResponseTrailerMap&, envoy_stream_intel) {};

  absl::AnyInvocable<void(envoy_stream_intel, envoy_final_stream_intel)> on_complete_ =
      [](envoy_stream_intel, envoy_final_stream_intel) {};

  absl::AnyInvocable<void(EnvoyError, envoy_stream_intel, envoy_final_stream_intel)> on_error_ =
      [](EnvoyError, envoy_stream_intel, envoy_final_stream_intel) {};

  absl::AnyInvocable<void(envoy_stream_intel, envoy_final_stream_intel)> on_cancel_ =
      [](envoy_stream_intel, envoy_final_stream_intel) {};

  absl::AnyInvocable<void(envoy_stream_intel)> on_send_window_available_ = [](envoy_stream_intel) {
  };
};

/** Networks classified by the physical link. */
enum class NetworkType : int {
  // This is the default and includes cases where network characteristics are unknown.
  Generic = 0,
  // This includes WiFi and other local area wireless networks.
  WLAN = 1,
  // This includes all mobile phone networks.
  WWAN = 2,
};

} // namespace Envoy
