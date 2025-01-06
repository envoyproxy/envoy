#include "source/extensions/filters/http/dynamic_modules/filter.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

void DynamicModuleHttpFilter::initializeInModuleFilter() {
  in_module_filter_ = config_->on_http_filter_new_(config_->in_module_config_, thisAsVoidPtr());
}

void DynamicModuleHttpFilter::onStreamComplete() {}

void DynamicModuleHttpFilter::onDestroy() { config_->on_http_filter_destroy_(in_module_filter_); };

FilterHeadersStatus DynamicModuleHttpFilter::decodeHeaders(RequestHeaderMap& headers,
                                                           bool end_of_stream) {
  if (sent_local_reply_) {
    return FilterHeadersStatus::Continue;
  }

  request_headers_ = &headers;
  const envoy_dynamic_module_type_on_http_filter_request_headers_status status =
      config_->on_http_filter_request_headers_(thisAsVoidPtr(), in_module_filter_, end_of_stream);
  return static_cast<FilterHeadersStatus>(status);
};

FilterDataStatus DynamicModuleHttpFilter::decodeData(Buffer::Instance&, bool end_of_stream) {
  if (sent_local_reply_) {
    return FilterDataStatus::Continue;
  }

  const envoy_dynamic_module_type_on_http_filter_request_body_status status =
      config_->on_http_filter_request_body_(thisAsVoidPtr(), in_module_filter_, end_of_stream);
  return static_cast<FilterDataStatus>(status);
};

FilterTrailersStatus DynamicModuleHttpFilter::decodeTrailers(RequestTrailerMap& trailers) {
  if (sent_local_reply_) {
    return FilterTrailersStatus::Continue;
  }

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
  if (sent_local_reply_) {
    return FilterHeadersStatus::Continue;
  }

  response_headers_ = &headers;
  const envoy_dynamic_module_type_on_http_filter_response_headers_status status =
      config_->on_http_filter_response_headers_(thisAsVoidPtr(), in_module_filter_, end_of_stream);
  return static_cast<FilterHeadersStatus>(status);
};

FilterDataStatus DynamicModuleHttpFilter::encodeData(Buffer::Instance&, bool end_of_stream) {
  if (sent_local_reply_) {
    return FilterDataStatus::Continue;
  }

  const envoy_dynamic_module_type_on_http_filter_response_body_status status =
      config_->on_http_filter_response_body_(thisAsVoidPtr(), in_module_filter_, end_of_stream);
  return static_cast<FilterDataStatus>(status);
};

FilterTrailersStatus DynamicModuleHttpFilter::encodeTrailers(ResponseTrailerMap& trailers) {
  if (sent_local_reply_) {
    return FilterTrailersStatus::Continue;
  }

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
  if (sent_local_reply_) {
    return;
  }

  sent_local_reply_ = true;
  decoder_callbacks_->sendLocalReply(code, body, modify_headers, grpc_status, details);
}

void DynamicModuleHttpFilter::encodeComplete(){};

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
