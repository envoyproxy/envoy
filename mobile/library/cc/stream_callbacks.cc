#include "stream_callbacks.h"

#include "bridge_utility.h"
#include "response_headers_builder.h"
#include "response_trailers_builder.h"

namespace Envoy {
namespace Platform {

namespace {

void* c_on_headers(envoy_headers headers, bool end_stream, void* context) {
  auto stream_callbacks = static_cast<StreamCallbacks*>(context);
  if (stream_callbacks->on_headers.has_value()) {
    auto raw_headers = envoy_headers_as_raw_header_map(headers);
    ResponseHeadersBuilder builder;
    for (const auto& pair : raw_headers) {
      if (pair.first == ":status") {
        builder.add_http_status(std::stoi(pair.second[0]));
      }
      builder.set(pair.first, pair.second);
    }
    auto on_headers = stream_callbacks->on_headers.value();
    on_headers(builder.build(), end_stream);
  }
  return context;
}

void* c_on_data(envoy_data data, bool end_stream, void* context) {
  auto stream_callbacks = static_cast<StreamCallbacks*>(context);
  if (stream_callbacks->on_data.has_value()) {
    auto on_data = stream_callbacks->on_data.value();
    on_data(data, end_stream);
  }
  return context;
}

void* c_on_trailers(envoy_headers metadata, void* context) {
  auto stream_callbacks = static_cast<StreamCallbacks*>(context);
  if (stream_callbacks->on_trailers.has_value()) {
    auto raw_headers = envoy_headers_as_raw_header_map(metadata);
    ResponseTrailersBuilder builder;
    for (const auto& pair : raw_headers) {
      builder.set(pair.first, pair.second);
    }
    auto on_trailers = stream_callbacks->on_trailers.value();
    on_trailers(builder.build());
  }
  return context;
}

void* c_on_error(envoy_error raw_error, void* context) {
  auto stream_callbacks = static_cast<StreamCallbacks*>(context);
  if (stream_callbacks->on_error.has_value()) {
    EnvoyErrorSharedPtr error = std::make_shared<EnvoyError>();
    error->error_code = raw_error.error_code;
    // TODO(crockeo): go back and convert from raw_error.message
    // when doing so won't cause merge conflicts with other PRs.
    error->message = "";
    error->attempt_count = absl::optional<int>(raw_error.attempt_count);
    auto on_error = stream_callbacks->on_error.value();
    on_error(error);
  }
  return context;
}

void* c_on_complete(void* context) {
  auto stream_callbacks = static_cast<StreamCallbacks*>(context);
  if (stream_callbacks->on_complete.has_value()) {
    auto on_complete = stream_callbacks->on_complete.value();
    on_complete();
  }
  return context;
}

void* c_on_cancel(void* context) {
  auto stream_callbacks = static_cast<StreamCallbacks*>(context);
  if (stream_callbacks->on_cancel.has_value()) {
    auto on_cancel = stream_callbacks->on_cancel.value();
    on_cancel();
  }
  return context;
}

} // namespace

envoy_http_callbacks StreamCallbacks::as_envoy_http_callbacks() {
  return envoy_http_callbacks{
      .on_headers = &c_on_headers,
      .on_data = &c_on_data,
      // on_metadata is not used
      .on_trailers = &c_on_trailers,
      .on_error = &c_on_error,
      .on_complete = &c_on_complete,
      .on_cancel = &c_on_cancel,
      .context = this,
  };
}

} // namespace Platform
} // namespace Envoy
