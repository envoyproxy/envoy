#include "source/extensions/filters/http/dynamic_modules/filter.h"

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

void DynamicModuleHttpFilter::onDestroy() { destroy(); };

void DynamicModuleHttpFilter::destroy() {
  if (in_module_filter_ == nullptr) {
    return;
  }
  config_->on_http_filter_destroy_(in_module_filter_);
  in_module_filter_ = nullptr;
  decoder_callbacks_ = nullptr;
  encoder_callbacks_ = nullptr;
  for (auto& callout : http_callouts_) {
    if (callout.second->request_) {
      callout.second->request_->cancel();
    }
  }
  http_callouts_.clear();
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
DynamicModuleHttpFilter::sendHttpCallout(uint32_t callout_id, absl::string_view cluster_name,
                                         Http::RequestMessagePtr&& message,
                                         uint64_t timeout_milliseconds) {
  Upstream::ThreadLocalCluster* cluster =
      config_->cluster_manager_.getThreadLocalCluster(cluster_name);
  if (!cluster) {
    return envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound;
  }
  Http::AsyncClient::RequestOptions options;
  options.setTimeout(std::chrono::milliseconds(timeout_milliseconds));
  auto [iterator, inserted] = http_callouts_.try_emplace(
      callout_id, std::make_unique<DynamicModuleHttpFilter::HttpCalloutCallback>(shared_from_this(),
                                                                                 callout_id));
  if (!inserted) {
    return envoy_dynamic_module_type_http_callout_init_result_DuplicateCalloutId;
  }
  DynamicModuleHttpFilter::HttpCalloutCallback& callback = *iterator->second;
  auto request = cluster->httpAsyncClient().send(std::move(message), callback, options);
  if (!request) {
    return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
  }
  callback.request_ = request;
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
  uint32_t callout_id = callout_id_;
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

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
