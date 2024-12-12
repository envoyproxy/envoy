#include "source/extensions/filters/http/dynamic_modules/filter.h"

#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

void DynamicModuleHttpFilter::initializeInModuleFilter() {
  in_module_filter_ =
      dynamic_module_->on_http_filter_new_(dynamic_module_->in_module_config_, thisAsVoidPtr());
}

void DynamicModuleHttpFilter::onStreamComplete() {}

void DynamicModuleHttpFilter::onDestroy() {
  dynamic_module_->on_http_filter_destroy_(in_module_filter_);
};

FilterHeadersStatus DynamicModuleHttpFilter::decodeHeaders(RequestHeaderMap&, bool end_of_stream) {
  const envoy_dynamic_module_type_on_http_filter_request_headers_status status =
      dynamic_module_->on_http_filter_request_headers_(thisAsVoidPtr(), in_module_filter_,
                                                       end_of_stream);
  return static_cast<FilterHeadersStatus>(status);
};

FilterDataStatus DynamicModuleHttpFilter::decodeData(Buffer::Instance&, bool end_of_stream) {
  const envoy_dynamic_module_type_on_http_filter_request_body_status status =
      dynamic_module_->on_http_filter_request_body_(thisAsVoidPtr(), in_module_filter_,
                                                    end_of_stream);
  return static_cast<FilterDataStatus>(status);
};

FilterTrailersStatus DynamicModuleHttpFilter::decodeTrailers(RequestTrailerMap&) {
  const envoy_dynamic_module_type_on_http_filter_request_trailers_status status =
      dynamic_module_->on_http_filter_request_trailers_(thisAsVoidPtr(), in_module_filter_);
  return static_cast<FilterTrailersStatus>(status);
}

FilterMetadataStatus DynamicModuleHttpFilter::decodeMetadata(MetadataMap&) {
  return FilterMetadataStatus::Continue;
}

void DynamicModuleHttpFilter::decodeComplete() {}

Filter1xxHeadersStatus DynamicModuleHttpFilter::encode1xxHeaders(ResponseHeaderMap&) {
  return Filter1xxHeadersStatus::Continue;
}

FilterHeadersStatus DynamicModuleHttpFilter::encodeHeaders(ResponseHeaderMap&, bool end_of_stream) {
  const envoy_dynamic_module_type_on_http_filter_response_headers_status status =
      dynamic_module_->on_http_filter_response_headers_(thisAsVoidPtr(), in_module_filter_,
                                                        end_of_stream);
  return static_cast<FilterHeadersStatus>(status);
};

FilterDataStatus DynamicModuleHttpFilter::encodeData(Buffer::Instance&, bool end_of_stream) {
  const envoy_dynamic_module_type_on_http_filter_response_body_status status =
      dynamic_module_->on_http_filter_response_body_(thisAsVoidPtr(), in_module_filter_,
                                                     end_of_stream);
  return static_cast<FilterDataStatus>(status);
};

FilterTrailersStatus DynamicModuleHttpFilter::encodeTrailers(ResponseTrailerMap&) {
  const envoy_dynamic_module_type_on_http_filter_response_trailers_status status =
      dynamic_module_->on_http_filter_response_trailers_(thisAsVoidPtr(), in_module_filter_);
  return static_cast<FilterTrailersStatus>(status);
};

FilterMetadataStatus DynamicModuleHttpFilter::encodeMetadata(MetadataMap&) {
  return FilterMetadataStatus::Continue;
}

void DynamicModuleHttpFilter::encodeComplete(){};

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
