#include "stream_callbacks.h"

#include "bridge_utility.h"
#include "library/common/data/utility.h"
#include "response_headers_builder.h"
#include "response_trailers_builder.h"

namespace Envoy {
namespace Platform {

namespace {

void* c_on_headers(envoy_headers headers, bool end_stream, void* context) {
  auto stream_callbacks = *static_cast<StreamCallbacksSharedPtr*>(context);
  if (stream_callbacks->on_headers.has_value()) {
    auto raw_headers = envoyHeadersAsRawHeaderMap(headers);
    ResponseHeadersBuilder builder;
    for (const auto& pair : raw_headers) {
      if (pair.first == ":status") {
        builder.addHttpStatus(std::stoi(pair.second[0]));
      }
      builder.set(pair.first, pair.second);
    }
    auto on_headers = stream_callbacks->on_headers.value();
    on_headers(builder.build(), end_stream);
  }
  return context;
}

void* c_on_data(envoy_data data, bool end_stream, void* context) {
  auto stream_callbacks = *static_cast<StreamCallbacksSharedPtr*>(context);
  if (stream_callbacks->on_data.has_value()) {
    auto on_data = stream_callbacks->on_data.value();
    on_data(data, end_stream);
  }
  return context;
}

void* c_on_trailers(envoy_headers metadata, void* context) {
  auto stream_callbacks = *static_cast<StreamCallbacksSharedPtr*>(context);
  if (stream_callbacks->on_trailers.has_value()) {
    auto raw_headers = envoyHeadersAsRawHeaderMap(metadata);
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
  auto stream_callbacks_ptr = static_cast<StreamCallbacksSharedPtr*>(context);
  auto stream_callbacks = *stream_callbacks_ptr;
  if (stream_callbacks->on_error.has_value()) {
    EnvoyErrorSharedPtr error = std::make_shared<EnvoyError>();
    error->error_code = raw_error.error_code;
    error->message = Data::Utility::copyToString(raw_error.message);
    error->attempt_count = absl::optional<int>(raw_error.attempt_count);
    auto on_error = stream_callbacks->on_error.value();
    on_error(error);
  }
  delete stream_callbacks_ptr;
  return nullptr;
}

void* c_on_complete(void* context) {
  auto stream_callbacks_ptr = static_cast<StreamCallbacksSharedPtr*>(context);
  auto stream_callbacks = *stream_callbacks_ptr;
  if (stream_callbacks->on_complete.has_value()) {
    auto on_complete = stream_callbacks->on_complete.value();
    on_complete();
  }
  delete stream_callbacks_ptr;
  return nullptr;
}

void* c_on_cancel(void* context) {
  auto stream_callbacks_ptr = static_cast<StreamCallbacksSharedPtr*>(context);
  auto stream_callbacks = *stream_callbacks_ptr;
  if (stream_callbacks->on_cancel.has_value()) {
    auto on_cancel = stream_callbacks->on_cancel.value();
    on_cancel();
  }
  delete stream_callbacks_ptr;
  return nullptr;
}

} // namespace

envoy_http_callbacks StreamCallbacks::asEnvoyHttpCallbacks() {
  return envoy_http_callbacks{
      .on_headers = &c_on_headers,
      .on_data = &c_on_data,
      .on_metadata = nullptr,
      .on_trailers = &c_on_trailers,
      .on_error = &c_on_error,
      .on_complete = &c_on_complete,
      .on_cancel = &c_on_cancel,
      .context = new StreamCallbacksSharedPtr(this->shared_from_this()),
  };
}

} // namespace Platform
} // namespace Envoy
