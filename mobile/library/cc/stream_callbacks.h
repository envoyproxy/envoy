#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "absl/types/optional.h"
#include "envoy_error.h"
#include "library/common/types/c_types.h"
#include "response_headers.h"
#include "response_trailers.h"

namespace Envoy {
namespace Platform {

using OnHeadersCallback = std::function<void(ResponseHeadersSharedPtr headers, bool end_stream)>;
using OnDataCallback = std::function<void(envoy_data data, bool end_stream)>;
using OnTrailersCallback = std::function<void(ResponseTrailersSharedPtr trailers)>;
using OnErrorCallback = std::function<void(EnvoyErrorSharedPtr error)>;
using OnCompleteCallback = std::function<void()>;
using OnCancelCallback = std::function<void()>;

struct StreamCallbacks {
  absl::optional<OnHeadersCallback> on_headers;
  absl::optional<OnDataCallback> on_data;
  absl::optional<OnTrailersCallback> on_trailers;
  absl::optional<OnErrorCallback> on_error;
  absl::optional<OnCompleteCallback> on_complete;
  absl::optional<OnCancelCallback> on_cancel;

  envoy_http_callbacks as_envoy_http_callbacks();
};

using StreamCallbacksSharedPtr = std::shared_ptr<StreamCallbacks>;

} // namespace Platform
} // namespace Envoy
