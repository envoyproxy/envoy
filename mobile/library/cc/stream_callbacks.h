#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "absl/types/optional.h"
#include "envoy_error.h"
#include "library/common/types/c_types.h"
#include "response_headers.h"
#include "response_trailers.h"
#include "stream.h"

namespace Envoy {
namespace Platform {

class Stream;
using StreamSharedPtr = std::shared_ptr<Stream>;

using OnHeadersCallback = std::function<void(ResponseHeadersSharedPtr headers, bool end_stream)>;
using OnDataCallback = std::function<void(envoy_data data, bool end_stream)>;
using OnTrailersCallback = std::function<void(ResponseTrailersSharedPtr trailers)>;
using OnErrorCallback = std::function<void(EnvoyErrorSharedPtr error)>;
using OnCompleteCallback = std::function<void()>;
using OnCancelCallback = std::function<void()>;
using OnSendWindowAvailableCallback = std::function<void()>;

// See library/common/types/c_types.h for what these callbacks should do.
struct StreamCallbacks : public std::enable_shared_from_this<StreamCallbacks> {
  absl::optional<OnHeadersCallback> on_headers;
  absl::optional<OnDataCallback> on_data;
  absl::optional<OnTrailersCallback> on_trailers;
  absl::optional<OnErrorCallback> on_error;
  absl::optional<OnCompleteCallback> on_complete;
  absl::optional<OnCancelCallback> on_cancel;
  absl::optional<OnSendWindowAvailableCallback> on_send_window_available;

  envoy_http_callbacks asEnvoyHttpCallbacks();
};

using StreamCallbacksSharedPtr = std::shared_ptr<StreamCallbacks>;

} // namespace Platform
} // namespace Envoy
