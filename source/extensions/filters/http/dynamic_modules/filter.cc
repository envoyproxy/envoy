#include "source/extensions/filters/http/dynamic_modules/filter.h"

#include <cstdint>
#include <memory>

#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

DynamicModuleHttpFilter::~DynamicModuleHttpFilter() { destroy(); }

void DynamicModuleHttpFilter::initializeInModuleFilter() {
  in_module_filter_ = config_->on_http_filter_new_(config_->in_module_config_, thisAsVoidPtr());
}

void DynamicModuleHttpFilter::onStreamComplete() {
  config_->on_http_filter_stream_complete_(thisAsVoidPtr(), in_module_filter_);
}

void DynamicModuleHttpFilter::onDestroy() {
  destroyed_ = true;
  destroy();
};

void DynamicModuleHttpFilter::destroy() {
  if (in_module_filter_ == nullptr) {
    return;
  }

  Event::Dispatcher* dispatcher = nullptr;
  if (!http_stream_callouts_.empty()) {
    if (decoder_callbacks_ != nullptr) {
      dispatcher = &decoder_callbacks_->dispatcher();
    } else if (encoder_callbacks_ != nullptr) {
      dispatcher = &encoder_callbacks_->dispatcher();
    }
  }

  config_->on_http_filter_destroy_(in_module_filter_);
  in_module_filter_ = nullptr;

  while (!http_callouts_.empty()) {
    auto it = http_callouts_.begin();
    if (it->second->request_) {
      it->second->request_->cancel();
    }
    if (!http_callouts_.empty() && http_callouts_.begin() == it) {
      http_callouts_.erase(it);
    }
  }

  while (!http_stream_callouts_.empty()) {
    auto it = http_stream_callouts_.begin();
    if (it->second->stream_) {
      it->second->stream_->reset();
    }
    // Do not delete the callback inline because AsyncClient may invoke it synchronously from reset.
    if (dispatcher != nullptr) {
      std::unique_ptr<Event::DeferredDeletable> deletable(it->second.release());
      dispatcher->deferredDelete(std::move(deletable));
    } else {
      it->second.reset();
    }
    if (!http_stream_callouts_.empty() && http_stream_callouts_.begin() == it) {
      http_stream_callouts_.erase(it);
    }
  }

  decoder_callbacks_ = nullptr;
  encoder_callbacks_ = nullptr;
}

FilterHeadersStatus DynamicModuleHttpFilter::decodeHeaders(RequestHeaderMap&, bool end_of_stream) {
  const envoy_dynamic_module_type_on_http_filter_request_headers_status status =
      config_->on_http_filter_request_headers_(thisAsVoidPtr(), in_module_filter_, end_of_stream);
  in_continue_ = status == envoy_dynamic_module_type_on_http_filter_request_headers_status_Continue;
  return static_cast<FilterHeadersStatus>(status);
};

FilterDataStatus DynamicModuleHttpFilter::decodeData(Buffer::Instance& chunk, bool end_of_stream) {
  if (end_of_stream && decoder_callbacks_->decodingBuffer()) {
    // To make the very last chunk of the body available to the filter when buffering is enabled,
    // we need to call addDecodedData. See the code comment there for more details.
    decoder_callbacks_->addDecodedData(chunk, false);
  }
  current_request_body_ = &chunk;
  const envoy_dynamic_module_type_on_http_filter_request_body_status status =
      config_->on_http_filter_request_body_(thisAsVoidPtr(), in_module_filter_, end_of_stream);
  current_request_body_ = nullptr;
  in_continue_ = status == envoy_dynamic_module_type_on_http_filter_request_body_status_Continue;
  return static_cast<FilterDataStatus>(status);
};

FilterTrailersStatus DynamicModuleHttpFilter::decodeTrailers(RequestTrailerMap&) {
  const envoy_dynamic_module_type_on_http_filter_request_trailers_status status =
      config_->on_http_filter_request_trailers_(thisAsVoidPtr(), in_module_filter_);
  in_continue_ =
      status == envoy_dynamic_module_type_on_http_filter_request_trailers_status_Continue;
  return static_cast<FilterTrailersStatus>(status);
}

FilterMetadataStatus DynamicModuleHttpFilter::decodeMetadata(MetadataMap&) {
  in_continue_ = true;
  return FilterMetadataStatus::Continue;
}

void DynamicModuleHttpFilter::decodeComplete() {}

Filter1xxHeadersStatus DynamicModuleHttpFilter::encode1xxHeaders(ResponseHeaderMap&) {
  in_continue_ = true;
  return Filter1xxHeadersStatus::Continue;
}

FilterHeadersStatus DynamicModuleHttpFilter::encodeHeaders(ResponseHeaderMap&, bool end_of_stream) {
  if (sent_local_reply_) { // See the comment on the flag.
    return FilterHeadersStatus::Continue;
  }
  const envoy_dynamic_module_type_on_http_filter_response_headers_status status =
      config_->on_http_filter_response_headers_(thisAsVoidPtr(), in_module_filter_, end_of_stream);
  in_continue_ =
      status == envoy_dynamic_module_type_on_http_filter_response_headers_status_Continue;
  return static_cast<FilterHeadersStatus>(status);
};

FilterDataStatus DynamicModuleHttpFilter::encodeData(Buffer::Instance& chunk, bool end_of_stream) {
  if (sent_local_reply_) { // See the comment on the flag.
    return FilterDataStatus::Continue;
  }
  if (end_of_stream && encoder_callbacks_->encodingBuffer()) {
    // To make the very last chunk of the body available to the filter when buffering is enabled,
    // we need to call addEncodedData. See the code comment there for more details.
    encoder_callbacks_->addEncodedData(chunk, false);
  }
  current_response_body_ = &chunk;
  const envoy_dynamic_module_type_on_http_filter_response_body_status status =
      config_->on_http_filter_response_body_(thisAsVoidPtr(), in_module_filter_, end_of_stream);
  current_response_body_ = nullptr;
  in_continue_ = status == envoy_dynamic_module_type_on_http_filter_response_body_status_Continue;
  return static_cast<FilterDataStatus>(status);
};

FilterTrailersStatus DynamicModuleHttpFilter::encodeTrailers(ResponseTrailerMap&) {
  if (sent_local_reply_) { // See the comment on the flag.
    return FilterTrailersStatus::Continue;
  }
  const envoy_dynamic_module_type_on_http_filter_response_trailers_status status =
      config_->on_http_filter_response_trailers_(thisAsVoidPtr(), in_module_filter_);
  in_continue_ =
      status == envoy_dynamic_module_type_on_http_filter_response_trailers_status_Continue;
  return static_cast<FilterTrailersStatus>(status);
};

FilterMetadataStatus DynamicModuleHttpFilter::encodeMetadata(MetadataMap&) {
  in_continue_ = true;
  return FilterMetadataStatus::Continue;
}

void DynamicModuleHttpFilter::sendLocalReply(
    Code code, absl::string_view body,
    std::function<void(ResponseHeaderMap& headers)> modify_headers,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details) {
  sent_local_reply_ = true;
  decoder_callbacks_->sendLocalReply(code, body, modify_headers, grpc_status, details);
}

void DynamicModuleHttpFilter::encodeComplete() {};

envoy_dynamic_module_type_http_callout_init_result
DynamicModuleHttpFilter::sendHttpCallout(uint64_t* callout_id_out, absl::string_view cluster_name,
                                         Http::RequestMessagePtr&& message,
                                         uint64_t timeout_milliseconds) {
  Upstream::ThreadLocalCluster* cluster =
      config_->cluster_manager_.getThreadLocalCluster(cluster_name);
  if (!cluster) {
    return envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound;
  }
  Http::AsyncClient::RequestOptions options;
  options.setTimeout(std::chrono::milliseconds(timeout_milliseconds));

  // Prepare the callback and the ID.
  const uint64_t callout_id = getNextCalloutId();
  auto http_callout_callabck = std::make_unique<DynamicModuleHttpFilter::HttpCalloutCallback>(
      shared_from_this(), callout_id);
  DynamicModuleHttpFilter::HttpCalloutCallback& callback = *http_callout_callabck;

  auto request = cluster->httpAsyncClient().send(std::move(message), callback, options);
  if (!request) {
    return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
  }

  // Register the callout.
  callback.request_ = request;
  http_callouts_.emplace(callout_id, std::move(http_callout_callabck));
  *callout_id_out = callout_id;

  return envoy_dynamic_module_type_http_callout_init_result_Success;
}

void DynamicModuleHttpFilter::HttpCalloutCallback::onSuccess(const AsyncClient::Request&,
                                                             ResponseMessagePtr&& response) {
  // Move the filter and callout id to the local scope since on_http_filter_http_callout_done_ might
  // results in the local reply which destroys the filter. That eventually ends up deallocating this
  // callback itself.
  DynamicModuleHttpFilterSharedPtr filter = std::move(filter_);
  uint32_t callout_id = callout_id_;
  // Check if the filter is destroyed before the callout completed.
  if (!filter->in_module_filter_) {
    return;
  }

  absl::InlinedVector<envoy_dynamic_module_type_envoy_http_header, 16> headers_vector;
  headers_vector.reserve(response->headers().size());
  response->headers().iterate([&headers_vector](
                                  const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    headers_vector.emplace_back(envoy_dynamic_module_type_envoy_http_header{
        const_cast<char*>(header.key().getStringView().data()), header.key().getStringView().size(),
        const_cast<char*>(header.value().getStringView().data()),
        header.value().getStringView().size()});
    return Http::HeaderMap::Iterate::Continue;
  });

  Envoy::Buffer::RawSliceVector body = response->body().getRawSlices(std::nullopt);
  filter->config_->on_http_filter_http_callout_done_(
      filter->thisAsVoidPtr(), filter->in_module_filter_, callout_id,
      envoy_dynamic_module_type_http_callout_result_Success, headers_vector.data(),
      headers_vector.size(), reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(body.data()),
      body.size());
  // Clean up the callout.
  filter->http_callouts_.erase(callout_id);
}

void DynamicModuleHttpFilter::HttpCalloutCallback::onFailure(
    const AsyncClient::Request&, Http::AsyncClient::FailureReason reason) {
  // Move the filter and callout id to the local scope since on_http_filter_http_callout_done_ might
  // results in the local reply which destroys the filter. That eventually ends up deallocating this
  // callback itself.
  DynamicModuleHttpFilterSharedPtr filter = std::move(filter_);
  const uint64_t callout_id = callout_id_;
  // Check if the filter is destroyed before the callout completed.
  if (!filter->in_module_filter_) {
    return;
  }
  // request_ is not null if the callout is actually sent to the upstream cluster.
  // This allows us to avoid inlined calls to onFailure() method (which results in a reentrant to
  // the modules) when the async client immediately fails the callout.
  if (request_) {
    envoy_dynamic_module_type_http_callout_result result;
    switch (reason) {
    case Http::AsyncClient::FailureReason::Reset:
      result = envoy_dynamic_module_type_http_callout_result_Reset;
      break;
    case Http::AsyncClient::FailureReason::ExceedResponseBufferLimit:
      result = envoy_dynamic_module_type_http_callout_result_ExceedResponseBufferLimit;
      break;
    }
    filter->config_->on_http_filter_http_callout_done_(filter->thisAsVoidPtr(),
                                                       filter->in_module_filter_, callout_id,
                                                       result, nullptr, 0, nullptr, 0);
  }

  // Clean up the callout.
  filter->http_callouts_.erase(callout_id);
}

void DynamicModuleHttpFilter::onScheduled(uint64_t event_id) {
  // By the time this event is invoked, the filter might be destroyed.
  if (in_module_filter_) {
    config_->on_http_filter_scheduled_(thisAsVoidPtr(), in_module_filter_, event_id);
  }
}

void DynamicModuleHttpFilter::continueDecoding() {
  if (decoder_callbacks_ && !in_continue_) {
    decoder_callbacks_->continueDecoding();
    in_continue_ = true;
  }
}

void DynamicModuleHttpFilter::continueEncoding() {
  if (encoder_callbacks_ && !in_continue_) {
    encoder_callbacks_->continueEncoding();
    in_continue_ = true;
  }
}

void DynamicModuleHttpFilter::onAboveWriteBufferHighWatermark() {
  config_->on_http_filter_downstream_above_write_buffer_high_watermark_(thisAsVoidPtr(),
                                                                        in_module_filter_);
}

void DynamicModuleHttpFilter::onBelowWriteBufferLowWatermark() {
  config_->on_http_filter_downstream_below_write_buffer_low_watermark_(thisAsVoidPtr(),
                                                                       in_module_filter_);
}

envoy_dynamic_module_type_http_callout_init_result
DynamicModuleHttpFilter::startHttpStream(uint64_t* stream_id_out, absl::string_view cluster_name,
                                         Http::RequestMessagePtr&& message, bool end_stream,
                                         uint64_t timeout_milliseconds) {
  // Get the cluster.
  Upstream::ThreadLocalCluster* cluster =
      config_->cluster_manager_.getThreadLocalCluster(cluster_name);
  if (cluster == nullptr) {
    return envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound;
  }
  // Check required headers are present.
  if (!message->headers().Path() || !message->headers().Method() || !message->headers().Host()) {
    return envoy_dynamic_module_type_http_callout_init_result_MissingRequiredHeaders;
  }

  // Create the callback.
  const uint64_t callout_id = getNextCalloutId();
  auto callback = std::make_unique<DynamicModuleHttpFilter::HttpStreamCalloutCallback>(
      shared_from_this(), callout_id);
  DynamicModuleHttpFilter::HttpStreamCalloutCallback& callback_ref = *callback;
  // Store the callback first so if start fails inline, we can clean it up properly.
  http_stream_callouts_[callout_id] = std::move(callback);

  Http::AsyncClient::StreamOptions options;
  options.setTimeout(std::chrono::milliseconds(timeout_milliseconds));

  Http::AsyncClient::Stream* async_stream = cluster->httpAsyncClient().start(callback_ref, options);
  if (!async_stream) {
    // Failed to create the stream, clean up.
    http_stream_callouts_.erase(callout_id);
    return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
  }

  callback_ref.stream_ = async_stream;
  callback_ref.request_message_ = std::move(message);
  *stream_id_out = callout_id;

  // Send headers. The end_stream flag controls whether headers alone end the stream.
  // If body is provided, send it immediately.
  bool has_initial_body = callback_ref.request_message_->body().length() > 0;
  if (has_initial_body) {
    // Send headers without end_stream, then send body with the end_stream flag.
    callback_ref.stream_->sendHeaders(callback_ref.request_message_->headers(),
                                      false /* end_stream */);

    // The stream might reset inline while sending headers. Bail out if that happened.
    if (callback_ref.stream_ == nullptr) {
      return envoy_dynamic_module_type_http_callout_init_result_Success;
    }

    callback_ref.stream_->sendData(callback_ref.request_message_->body(), end_stream);
    if (callback_ref.stream_ == nullptr) {
      return envoy_dynamic_module_type_http_callout_init_result_Success;
    }
  } else {
    // No body, so end_stream applies to headers.
    callback_ref.stream_->sendHeaders(callback_ref.request_message_->headers(), end_stream);
    if (callback_ref.stream_ == nullptr) {
      return envoy_dynamic_module_type_http_callout_init_result_Success;
    }
  }

  return envoy_dynamic_module_type_http_callout_init_result_Success;
}

void DynamicModuleHttpFilter::resetHttpStream(uint64_t stream_id) {
  auto it = http_stream_callouts_.find(stream_id);
  if (it != http_stream_callouts_.end() && it->second->stream_) {
    it->second->stream_->reset();
  }
}

bool DynamicModuleHttpFilter::sendStreamData(uint64_t stream_id, Buffer::Instance& data,
                                             bool end_stream) {
  auto it = http_stream_callouts_.find(stream_id);
  if (it == http_stream_callouts_.end() || !it->second->stream_) {
    return false;
  }
  it->second->stream_->sendData(data, end_stream);
  return true;
}

bool DynamicModuleHttpFilter::sendStreamTrailers(uint64_t stream_id,
                                                 Http::RequestTrailerMapPtr trailers) {
  auto it = http_stream_callouts_.find(stream_id);
  if (it == http_stream_callouts_.end() || !it->second->stream_) {
    return false;
  }

  // Store the trailers in the callback to keep them alive, since AsyncStream stores a pointer.
  it->second->request_trailers_ = std::move(trailers);
  it->second->stream_->sendTrailers(*it->second->request_trailers_);
  return true;
}

void DynamicModuleHttpFilter::HttpStreamCalloutCallback::onHeaders(ResponseHeaderMapPtr&& headers,
                                                                   bool end_stream) {
  // Check if the filter is destroyed before the stream completes.
  if (!filter_->in_module_filter_) {
    return;
  }

  absl::InlinedVector<envoy_dynamic_module_type_envoy_http_header, 16> headers_vector;
  headers_vector.reserve(headers->size());
  headers->iterate([&headers_vector](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    headers_vector.emplace_back(envoy_dynamic_module_type_envoy_http_header{
        const_cast<char*>(header.key().getStringView().data()), header.key().getStringView().size(),
        const_cast<char*>(header.value().getStringView().data()),
        header.value().getStringView().size()});
    return Http::HeaderMap::Iterate::Continue;
  });

  filter_->config_->on_http_filter_http_stream_headers_(
      filter_->thisAsVoidPtr(), filter_->in_module_filter_, callout_id_, headers_vector.data(),
      headers_vector.size(), end_stream);
}

void DynamicModuleHttpFilter::HttpStreamCalloutCallback::onData(Buffer::Instance& data,
                                                                bool end_stream) {
  // Check if the filter is destroyed before the stream completes.
  if (!filter_->in_module_filter_) {
    return;
  }

  const uint64_t length = data.length();
  if (length > 0 || end_stream) {
    std::vector<envoy_dynamic_module_type_envoy_buffer> buffers;
    const auto& slices = data.getRawSlices();
    buffers.reserve(slices.size());
    for (const auto& slice : slices) {
      buffers.push_back({static_cast<char*>(slice.mem_), slice.len_});
    }
    filter_->config_->on_http_filter_http_stream_data_(filter_->thisAsVoidPtr(),
                                                       filter_->in_module_filter_, callout_id_,
                                                       buffers.data(), buffers.size(), end_stream);
  }
}

void DynamicModuleHttpFilter::HttpStreamCalloutCallback::onTrailers(
    ResponseTrailerMapPtr&& trailers) {
  // Check if the filter is destroyed before the stream completes.
  if (!filter_->in_module_filter_) {
    return;
  }

  absl::InlinedVector<envoy_dynamic_module_type_envoy_http_header, 16> trailers_vector;
  trailers_vector.reserve(trailers->size());
  trailers->iterate([&trailers_vector](
                        const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    trailers_vector.emplace_back(envoy_dynamic_module_type_envoy_http_header{
        const_cast<char*>(header.key().getStringView().data()), header.key().getStringView().size(),
        const_cast<char*>(header.value().getStringView().data()),
        header.value().getStringView().size()});
    return Http::HeaderMap::Iterate::Continue;
  });

  filter_->config_->on_http_filter_http_stream_trailers_(
      filter_->thisAsVoidPtr(), filter_->in_module_filter_, callout_id_, trailers_vector.data(),
      trailers_vector.size());
}

void DynamicModuleHttpFilter::HttpStreamCalloutCallback::onComplete() {
  // Avoid double cleanup if this callback was already handled.
  if (cleaned_up_) {
    return;
  }
  cleaned_up_ = true;

  // Move the filter to the local scope since on_http_filter_http_stream_complete_ might
  // result in a local reply which destroys the filter. That eventually ends up deallocating this
  // callback itself.
  DynamicModuleHttpFilterSharedPtr filter = std::move(filter_);

  // Check if the filter is destroyed before we can invoke the callback.
  if (!filter->in_module_filter_ || !filter->decoder_callbacks_) {
    return;
  }

  // Cache the dispatcher before we call the module callback, as the callback may destroy the filter
  // which will clear decoder_callbacks_.
  Event::Dispatcher& dispatcher = filter->decoder_callbacks_->dispatcher();

  filter->config_->on_http_filter_http_stream_complete_(filter->thisAsVoidPtr(),
                                                        filter->in_module_filter_, callout_id_);

  stream_ = nullptr;
  request_message_.reset();
  request_trailers_.reset();

  // Schedule deferred deletion of this callback to avoid deleting 'this' while we're still in it.
  // The stream may call other callbacks like onReset() after onComplete().
  auto it = filter->http_stream_callouts_.find(callout_id_);
  if (it != filter->http_stream_callouts_.end()) {
    // Cast unique_ptr<HttpStreamCalloutCallback> to unique_ptr<DeferredDeletable> for deferred
    // deletion.
    std::unique_ptr<Event::DeferredDeletable> deletable(it->second.release());
    dispatcher.deferredDelete(std::move(deletable));
    filter->http_stream_callouts_.erase(it);
  }
}

void DynamicModuleHttpFilter::HttpStreamCalloutCallback::onReset() {
  // Avoid double cleanup if this callback was already handled.
  if (cleaned_up_) {
    return;
  }
  cleaned_up_ = true;

  // Move the filter to the local scope since on_http_filter_http_stream_reset_ might
  // result in a local reply which destroys the filter. That eventually ends up deallocating this
  // callback itself.
  DynamicModuleHttpFilterSharedPtr filter = std::move(filter_);

  // Check if the filter is destroyed before we can invoke the callback.
  if (!filter->in_module_filter_ || !filter->decoder_callbacks_) {
    return;
  }

  // Cache the dispatcher before we call the module callback, as the callback may destroy the filter
  // which will clear decoder_callbacks_.
  Event::Dispatcher& dispatcher = filter->decoder_callbacks_->dispatcher();

  // Only invoke the callback if the stream was actually started.
  if (stream_) {
    // Since we don't have detailed reset reason here, use a generic one.
    filter->config_->on_http_filter_http_stream_reset_(
        filter->thisAsVoidPtr(), filter->in_module_filter_, callout_id_,
        envoy_dynamic_module_type_http_stream_reset_reason_LocalReset);
  }

  stream_ = nullptr;
  request_message_.reset();
  request_trailers_.reset();

  // Schedule deferred deletion of this callback to avoid deleting 'this' while we're still in it.
  auto it = filter->http_stream_callouts_.find(callout_id_);
  if (it != filter->http_stream_callouts_.end()) {
    // Cast unique_ptr<HttpStreamCalloutCallback> to unique_ptr<DeferredDeletable> for deferred
    // deletion.
    std::unique_ptr<Event::DeferredDeletable> deletable(it->second.release());
    dispatcher.deferredDelete(std::move(deletable));
    filter->http_stream_callouts_.erase(it);
  }
}

void DynamicModuleHttpFilter::storeSocketOptionInt(
    int64_t level, int64_t name, envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_socket_direction direction, int64_t value) {
  socket_options_.push_back(
      {level, name, state, direction, /*is_int=*/true, value, /*byte_value=*/std::string()});
}

void DynamicModuleHttpFilter::storeSocketOptionBytes(
    int64_t level, int64_t name, envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_socket_direction direction, absl::string_view value) {
  socket_options_.push_back(
      {level, name, state, direction, /*is_int=*/false, /*int_value=*/0, std::string(value)});
}

bool DynamicModuleHttpFilter::tryGetSocketOptionInt(
    int64_t level, int64_t name, envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_socket_direction direction, int64_t& value_out) const {
  for (const auto& opt : socket_options_) {
    if (opt.level == level && opt.name == name && opt.state == state &&
        opt.direction == direction && opt.is_int) {
      value_out = opt.int_value;
      return true;
    }
  }
  return false;
}

bool DynamicModuleHttpFilter::tryGetSocketOptionBytes(
    int64_t level, int64_t name, envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_socket_direction direction, absl::string_view& value_out) const {
  for (const auto& opt : socket_options_) {
    if (opt.level == level && opt.name == name && opt.state == state &&
        opt.direction == direction && !opt.is_int) {
      value_out = opt.byte_value;
      return true;
    }
  }
  return false;
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
