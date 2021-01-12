#include "stream_callbacks.h"

#include "bridge_utility.h"
#include "response_headers_builder.h"
#include "response_trailers_builder.h"

namespace Envoy {
namespace Platform {

EnvoyHttpCallbacksAdapter::EnvoyHttpCallbacksAdapter(StreamCallbacksSharedPtr callbacks)
    : stream_callbacks_(callbacks) {}

envoy_http_callbacks EnvoyHttpCallbacksAdapter::as_envoy_http_callbacks() {
  envoy_http_callbacks callbacks{
      .on_headers = &EnvoyHttpCallbacksAdapter::c_on_headers,
      .on_data = &EnvoyHttpCallbacksAdapter::c_on_data,
      // on_metadata is not used
      .on_trailers = &EnvoyHttpCallbacksAdapter::c_on_trailers,
      .on_error = &EnvoyHttpCallbacksAdapter::c_on_error,
      .on_complete = &EnvoyHttpCallbacksAdapter::c_on_complete,
      .on_cancel = &EnvoyHttpCallbacksAdapter::c_on_cancel,
      .context = this,
  };
  return callbacks;
}

void* EnvoyHttpCallbacksAdapter::c_on_headers(envoy_headers headers, bool end_stream,
                                              void* context) {
  auto self = static_cast<EnvoyHttpCallbacksAdapter*>(context);
  if (self->stream_callbacks_->on_headers.has_value()) {
    auto raw_headers = envoy_headers_as_raw_header_map(headers);
    ResponseHeadersBuilder builder;
    for (const auto& pair : raw_headers) {
      if (pair.first == ":status") {
        builder.add_http_status(std::stoi(pair.second[0]));
      }
      builder.set(pair.first, pair.second);
    }
    self->stream_callbacks_->on_headers.value()(builder.build(), end_stream);
  }
  return context;
}

void* EnvoyHttpCallbacksAdapter::c_on_data(envoy_data data, bool end_stream, void* context) {
  auto self = static_cast<EnvoyHttpCallbacksAdapter*>(context);
  if (self->stream_callbacks_->on_error.has_value()) {
    self->stream_callbacks_->on_data.value()(data, end_stream);
  }
  return context;
}

void* EnvoyHttpCallbacksAdapter::c_on_trailers(envoy_headers metadata, void* context) {
  auto self = static_cast<EnvoyHttpCallbacksAdapter*>(context);
  if (self->stream_callbacks_->on_trailers.has_value()) {
    auto raw_headers = envoy_headers_as_raw_header_map(metadata);
    ResponseTrailersBuilder builder;
    for (const auto& pair : raw_headers) {
      builder.set(pair.first, pair.second);
    }
    self->stream_callbacks_->on_trailers.value()(builder.build());
  }
  return context;
}

void* EnvoyHttpCallbacksAdapter::c_on_error(envoy_error raw_error, void* context) {
  auto self = static_cast<EnvoyHttpCallbacksAdapter*>(context);
  if (self->stream_callbacks_->on_error.has_value()) {
    EnvoyErrorSharedPtr error = std::make_shared<EnvoyError>();
    error->error_code = raw_error.error_code;
    // TODO(crockeo): go back and convert from raw_error.message
    // when doing so won't cause merge conflicts with other PRs.
    error->message = "";
    error->attempt_count = absl::optional<int>(raw_error.attempt_count);
    self->stream_callbacks_->on_error.value()(error);
  }
  return context;
}

void* EnvoyHttpCallbacksAdapter::c_on_complete(void* context) {
  auto self = static_cast<EnvoyHttpCallbacksAdapter*>(context);
  if (self->stream_callbacks_->on_complete.has_value()) {
    self->stream_callbacks_->on_complete.value();
  }
  return context;
}

void* EnvoyHttpCallbacksAdapter::c_on_cancel(void* context) {
  auto self = static_cast<EnvoyHttpCallbacksAdapter*>(context);
  if (self->stream_callbacks_->on_cancel.has_value()) {
    self->stream_callbacks_->on_cancel.value();
  }
  return context;
}

} // namespace Platform
} // namespace Envoy
