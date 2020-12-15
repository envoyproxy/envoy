#pragma once

// NOLINT(namespace-envoy)

#include <memory>
#include <optional>
#include <vector>

#include "absl/types/optional.h"
#include "envoy_error.h"
#include "executor.h"
#include "response_headers.h"
#include "response_trailers.h"

using OnHeadersCallback = std::function<void(ResponseHeadersSharedPtr headers, bool end_stream)>;
using OnDataCallback = std::function<void(std::vector<uint8_t> data, bool end_stream)>;
using OnTrailersCallback = std::function<void(ResponseTrailersSharedPtr trailers)>;
using OnCancelCallback = std::function<void()>;
using OnErrorCallback = std::function<void(EnvoyErrorSharedPtr error)>;

struct StreamCallbacks {
  absl::optional<OnHeadersCallback> on_headers;
  absl::optional<OnDataCallback> on_data;
  absl::optional<OnTrailersCallback> on_trailers;
  absl::optional<OnCancelCallback> on_cancel;
  absl::optional<OnErrorCallback> on_error;
};

using StreamCallbacksSharedPtr = std::shared_ptr<StreamCallbacks>;

class EnvoyHttpCallbacksAdapter {
public:
  EnvoyHttpCallbacksAdapter(ExecutorSharedPtr executor, StreamCallbacksSharedPtr callbacks);

private:
  ExecutorSharedPtr executor_;
  StreamCallbacksSharedPtr stream_callbacks_;
};

using EnvoyHttpCallbacksAdapterSharedPtr = std::shared_ptr<EnvoyHttpCallbacksAdapter>;
