#include "source/extensions/filters/http/dynamic_modules/filter.h"

#include <vector>

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

void DynamicModuleHttpFilter::HttpCalloutCallback::onSuccess(const AsyncClient::Request&,
                                                             ResponseMessagePtr&& response) {

  // Check if the filter is destroyed before the callout completed. Note that this HTTP filter
  // initiated callout is "thread-local", so checking the filter pointer is safe here.
  if (!filter_->in_module_filter_) {
    return;
  }

  std::vector<envoy_dynamic_module_type_http_header> headers_vector;
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
  filter_->config_->on_http_filter_http_callout_done_(
      filter_->thisAsVoidPtr(), filter_->in_module_filter_, callout_id_,
      envoy_dynamic_module_type_http_callout_result_Success, headers_vector.data(),
      headers_vector.size(), reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(body.data()),
      body.size());

  // This callback is allocated on the heap and not held by a shared pointer, so we need to
  // delete it here.
  delete this;
}

void DynamicModuleHttpFilter::HttpCalloutCallback::onFailure(
    const AsyncClient::Request&, Http::AsyncClient::FailureReason reason) {
  // Check if the filter is destroyed before the callout completed. Note that this HTTP filter
  // initiated callout is "thread-local", so checking the filter pointer is safe here.
  if (!filter_->in_module_filter_) {
    return;
  }
  if (!sent_) {
    // Immediate failure case. See the comment on fired() method.
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
  filter_->config_->on_http_filter_http_callout_done_(filter_->thisAsVoidPtr(),
                                                      filter_->in_module_filter_, callout_id_,
                                                      result, nullptr, 0, nullptr, 0);

  // This callback is allocated on the heap and not held by a shared pointer, so we need to
  // delete it here.
  delete this;
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
