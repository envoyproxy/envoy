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
  envoy_error_code_t error_code_;
  std::string message_;
  absl::optional<int> attempt_count_ = absl::nullopt;
};

/** The callbacks for the stream. */
struct EnvoyStreamCallbacks {
  /**
   * The callback for headers on a stream.
   *
   * The callback function passes the following parameters.
   * - headers: the headers received.
   * - end_stream: to indicate whether the response is headers-only.
   * - stream_intel: contains internal stream metrics.
   */
  absl::AnyInvocable<void(const Http::ResponseHeaderMap&, bool /* end_stream */,
                          envoy_stream_intel)>
      on_headers_ = [](const Http::ResponseHeaderMap&, bool, envoy_stream_intel) {};

  /**
   * The callback for data on a stream.
   *
   * This callback can be invoked multiple times when data is streamed.
   *
   * The callback function pases the following parameters.
   * - buffer: the data received.
   * - length: the length of data to read. It will always be <= `buffer.length()`
   * - end_stream: whether the data is the last data frame.
   * - stream_intel: contains internal stream metrics.
   */
  absl::AnyInvocable<void(const Buffer::Instance& buffer, uint64_t /* length */,
                          bool /* end_stream */, envoy_stream_intel)>
      on_data_ = [](const Buffer::Instance&, uint64_t, bool, envoy_stream_intel) {};

  /**
   * The callback for trailers on a stream.
   *
   * Note that end stream is implied when on_trailers is called.
   *
   * The callback function pases the following parameters.
   * - trailers: the trailers received.
   * - stream_intel: contains internal stream metrics.
   */
  absl::AnyInvocable<void(const Http::ResponseTrailerMap&, envoy_stream_intel)> on_trailers_ =
      [](const Http::ResponseTrailerMap&, envoy_stream_intel) {};

  /**
   * The callback for when a stream bi-directionally completes without error.
   *
   * This is a TERMINAL callback. Exactly one terminal callback will be called per stream.
   *
   * The callback function pases the following parameters.
   * - stream_intel: contains internal stream metrics.
   * - final_stream_intel: contains final internal stream metrics.
   */
  absl::AnyInvocable<void(envoy_stream_intel, envoy_final_stream_intel)> on_complete_ =
      [](envoy_stream_intel, envoy_final_stream_intel) {};

  /**
   * The callback for errors with a stream.
   *
   * This is a TERMINAL callback. Exactly one terminal callback will be called per stream.
   *
   * The callback function pases the following parameters.
   * - error: the error received/caused by the stream.
   * - stream_intel: contains internal stream metrics.
   * - final_stream_intel: contains final internal stream metrics.
   */
  absl::AnyInvocable<void(const EnvoyError&, envoy_stream_intel, envoy_final_stream_intel)>
      on_error_ = [](const EnvoyError&, envoy_stream_intel, envoy_final_stream_intel) {};

  /**
   * The callback for when the stream is cancelled.
   *
   * This is a TERMINAL callback. Exactly one terminal callback will be called per stream.
   *
   * The callback function pases the following parameters.
   * - stream_intel: contains internal stream metrics.
   * - final_stream_intel: contains final internal stream metrics.
   */
  absl::AnyInvocable<void(envoy_stream_intel, envoy_final_stream_intel)> on_cancel_ =
      [](envoy_stream_intel, envoy_final_stream_intel) {};

  /**
   * The callback which notifies when there is buffer available for request body upload.
   *
   * This is only ever called when the library is in explicit flow control mode. In explicit mode,
   * this will be called after the first call to decodeData, when more buffer is available locally
   * for request body. It will then be called once per decodeData call to inform the sender when it
   * is safe to send more data.
   *
   * The callback function pases the following parameters.
   * - stream_intel: contains internal stream metrics.
   */
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
