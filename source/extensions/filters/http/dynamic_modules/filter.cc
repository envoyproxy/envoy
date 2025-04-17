#include "source/extensions/filters/http/dynamic_modules/filter.h"

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
  for (auto& callout : http_callouts_) {
    if (callout.second.request_) {
      callout.second.request_->cancel();
    }
  }
  http_callouts_.clear();
}

FilterHeadersStatus DynamicModuleHttpFilter::decodeHeaders(RequestHeaderMap& headers,
                                                           bool end_of_stream) {
  request_headers_ = &headers;
  const envoy_dynamic_module_type_on_http_filter_request_headers_status status =
      config_->on_http_filter_request_headers_(thisAsVoidPtr(), in_module_filter_, end_of_stream);
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
  return static_cast<FilterDataStatus>(status);
};

FilterTrailersStatus DynamicModuleHttpFilter::decodeTrailers(RequestTrailerMap& trailers) {
  request_trailers_ = &trailers;
  const envoy_dynamic_module_type_on_http_filter_request_trailers_status status =
      config_->on_http_filter_request_trailers_(thisAsVoidPtr(), in_module_filter_);
  return static_cast<FilterTrailersStatus>(status);
}

FilterMetadataStatus DynamicModuleHttpFilter::decodeMetadata(MetadataMap&) {
  return FilterMetadataStatus::Continue;
}

void DynamicModuleHttpFilter::decodeComplete() {}

Filter1xxHeadersStatus DynamicModuleHttpFilter::encode1xxHeaders(ResponseHeaderMap&) {
  return Filter1xxHeadersStatus::Continue;
}

FilterHeadersStatus DynamicModuleHttpFilter::encodeHeaders(ResponseHeaderMap& headers,
                                                           bool end_of_stream) {
  response_headers_ = &headers;
  const envoy_dynamic_module_type_on_http_filter_response_headers_status status =
      config_->on_http_filter_response_headers_(thisAsVoidPtr(), in_module_filter_, end_of_stream);
  return static_cast<FilterHeadersStatus>(status);
};

FilterDataStatus DynamicModuleHttpFilter::encodeData(Buffer::Instance& chunk, bool end_of_stream) {
  if (end_of_stream && encoder_callbacks_->encodingBuffer()) {
    // To make the very last chunk of the body available to the filter when buffering is enabled,
    // we need to call addEncodedData. See the code comment there for more details.
    encoder_callbacks_->addEncodedData(chunk, false);
  }
  current_response_body_ = &chunk;
  const envoy_dynamic_module_type_on_http_filter_response_body_status status =
      config_->on_http_filter_response_body_(thisAsVoidPtr(), in_module_filter_, end_of_stream);
  current_response_body_ = nullptr;
  return static_cast<FilterDataStatus>(status);
};

FilterTrailersStatus DynamicModuleHttpFilter::encodeTrailers(ResponseTrailerMap& trailers) {
  response_trailers_ = &trailers;
  const envoy_dynamic_module_type_on_http_filter_response_trailers_status status =
      config_->on_http_filter_response_trailers_(thisAsVoidPtr(), in_module_filter_);
  return static_cast<FilterTrailersStatus>(status);
};

FilterMetadataStatus DynamicModuleHttpFilter::encodeMetadata(MetadataMap&) {
  return FilterMetadataStatus::Continue;
}

void DynamicModuleHttpFilter::sendLocalReply(
    Code code, absl::string_view body,
    std::function<void(ResponseHeaderMap& headers)> modify_headers,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details) {
  decoder_callbacks_->sendLocalReply(code, body, modify_headers, grpc_status, details);
}

void DynamicModuleHttpFilter::encodeComplete() {};

bool DynamicModuleHttpFilter::sendHttpCallout(uint32_t callout_id, absl::string_view cluster_name,
                                              Http::RequestMessagePtr&& message,
                                              uint64_t timeout_milliseconds) {
  Upstream::ThreadLocalCluster* cluster =
      config_->cluster_manager_.getThreadLocalCluster(cluster_name);
  if (!cluster) {
    ENVOY_LOG(error, "Cluster {} not found", cluster_name);
    return false;
  }
  Http::AsyncClient::RequestOptions options;
  options.setTimeout(std::chrono::milliseconds(timeout_milliseconds));
  auto [iterator, inserted] = http_callouts_.try_emplace(callout_id, *this, callout_id);
  if (!inserted) {
    ENVOY_LOG(error, "Duplicate callout id {}", callout_id);
    return false;
  }
  auto& callback = iterator->second;
  auto request = cluster->httpAsyncClient().send(std::move(message), callback, options);
  if (!request) {
    ENVOY_LOG(error, "Failed to send HTTP callout");
    return false;
  }
  callback.sent_ = true;
  callback.request_ = request;
  return true;
}

void DynamicModuleHttpFilter::HttpCalloutCallback::onSuccess(const AsyncClient::Request&,
                                                             ResponseMessagePtr&& response) {
  // Check if the filter is destroyed before the callout completed.
  if (!filter_.in_module_filter_) {
    return;
  }

  absl::InlinedVector<envoy_dynamic_module_type_http_header, 16> headers_vector;
  headers_vector.reserve(response->headers().size());
  response->headers().iterate([&headers_vector](
                                  const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    headers_vector.emplace_back(envoy_dynamic_module_type_http_header{
        const_cast<char*>(header.key().getStringView().data()), header.key().getStringView().size(),
        const_cast<char*>(header.value().getStringView().data()),
        header.value().getStringView().size()});
    return Http::HeaderMap::Iterate::Continue;
  });

  Envoy::Buffer::RawSliceVector body = response->body().getRawSlices(std::nullopt);
  filter_.config_->on_http_filter_http_callout_done_(
      filter_.thisAsVoidPtr(), filter_.in_module_filter_, callout_id_,
      envoy_dynamic_module_type_http_callout_result_Success, headers_vector.data(),
      headers_vector.size(), reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(body.data()),
      body.size());

  // Clean up the callout.
  filter_.http_callouts_.erase(callout_id_);
}

void DynamicModuleHttpFilter::HttpCalloutCallback::onFailure(
    const AsyncClient::Request&, Http::AsyncClient::FailureReason reason) {
  // Check if the filter is destroyed before the callout completed.
  if (!filter_.in_module_filter_ || !sent_) { // See the comment on sent_ for more details.
    return;
  }

  envoy_dynamic_module_type_http_callout_result result;
  switch (reason) {
  case Http::AsyncClient::FailureReason::Reset:
    result = envoy_dynamic_module_type_http_callout_result_Reset;
    break;
  case Http::AsyncClient::FailureReason::ExceedResponseBufferLimit:
    result = envoy_dynamic_module_type_http_callout_result_ExceedResponseBufferLimit;
    break;
  }
  filter_.config_->on_http_filter_http_callout_done_(filter_.thisAsVoidPtr(),
                                                     filter_.in_module_filter_, callout_id_, result,
                                                     nullptr, 0, nullptr, 0);
  // Clean up the callout.
  filter_.http_callouts_.erase(callout_id_);
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
