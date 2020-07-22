#include "library/common/extensions/filters/http/platform_bridge/filter.h"

#include "envoy/server/filter_config.h"

#include "common/common/assert.h"
#include "common/common/utility.h"

#include "library/common/api/external.h"
#include "library/common/buffer/bridge_fragment.h"
#include "library/common/buffer/utility.h"
#include "library/common/http/header_utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PlatformBridge {

PlatformBridgeFilterConfig::PlatformBridgeFilterConfig(
    const envoymobile::extensions::filters::http::platform_bridge::PlatformBridge& proto_config)
    : platform_filter_(static_cast<envoy_http_filter*>(
          Api::External::retrieveApi(proto_config.platform_filter_name()))) {}

PlatformBridgeFilter::PlatformBridgeFilter(PlatformBridgeFilterConfigSharedPtr config)
    : platform_filter_(config->platform_filter()) {}

Http::FilterHeadersStatus PlatformBridgeFilter::onHeaders(Http::HeaderMap& headers, bool end_stream,
                                                          envoy_filter_on_headers_f on_headers) {
  // Allow nullptr to act as (optimized) no-op.
  if (on_headers == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  envoy_headers in_headers = Http::Utility::toBridgeHeaders(headers);
  envoy_filter_headers_status result =
      on_headers(in_headers, end_stream, platform_filter_->context);
  Http::FilterHeadersStatus status = static_cast<Http::FilterHeadersStatus>(result.status);
  // TODO(goaway): Current platform implementations expose immutable headers, thus any modification
  // necessitates a full copy. Add 'modified' bit to determine when we can elide the copy. See also
  // https://github.com/lyft/envoy-mobile/issues/949 for potential future optimization.
  headers.clear();
  for (envoy_header_size_t i = 0; i < result.headers.length; i++) {
    headers.addCopy(
        Http::LowerCaseString(Http::Utility::convertToString(result.headers.headers[i].key)),
        Http::Utility::convertToString(result.headers.headers[i].value));
  }
  // The C envoy_headers struct can be released now because the headers have been copied.
  release_envoy_headers(result.headers);
  return status;
}

Http::FilterDataStatus PlatformBridgeFilter::onData(Buffer::Instance& data, bool end_stream,
                                                    envoy_filter_on_data_f on_data) {
  // Allow nullptr to act as (optimized) no-op.
  if (on_data == nullptr) {
    return Http::FilterDataStatus::Continue;
  }

  envoy_data in_data = Buffer::Utility::toBridgeData(data);
  envoy_filter_data_status result = on_data(in_data, end_stream, platform_filter_->context);
  Http::FilterDataStatus status = static_cast<Http::FilterDataStatus>(result.status);
  // Current platform implementations expose immutable data, thus any modification necessitates a
  // full copy. If the returned buffer is identical, we assume no modification was made and elide
  // the copy here. See also https://github.com/lyft/envoy-mobile/issues/949 for potential future
  // optimization.
  if (in_data.bytes != result.data.bytes) {
    data.drain(data.length());
    data.addBufferFragment(*Buffer::BridgeFragment::createBridgeFragment(result.data));
  }

  return status;
}

Http::FilterHeadersStatus PlatformBridgeFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool end_stream) {
  // Delegate to shared implementation for request and response path.
  return onHeaders(headers, end_stream, platform_filter_->on_request_headers);
}

Http::FilterDataStatus PlatformBridgeFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  // Delegate to shared implementation for request and response path.
  return onData(data, end_stream, platform_filter_->on_request_data);
}

Http::FilterTrailersStatus
PlatformBridgeFilter::decodeTrailers(Http::RequestTrailerMap& /*trailers*/) {
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterMetadataStatus PlatformBridgeFilter::decodeMetadata(Http::MetadataMap& /*metadata*/) {
  return Http::FilterMetadataStatus::Continue;
}

Http::FilterHeadersStatus
PlatformBridgeFilter::encode100ContinueHeaders(Http::ResponseHeaderMap& /*headers*/) {
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus PlatformBridgeFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                              bool end_stream) {
  // Delegate to shared implementation for request and response path.
  return onHeaders(headers, end_stream, platform_filter_->on_response_headers);
}

Http::FilterDataStatus PlatformBridgeFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  // Delegate to shared implementation for request and response path.
  return onData(data, end_stream, platform_filter_->on_response_data);
}

Http::FilterTrailersStatus
PlatformBridgeFilter::encodeTrailers(Http::ResponseTrailerMap& /*trailers*/) {
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterMetadataStatus PlatformBridgeFilter::encodeMetadata(Http::MetadataMap& /*metadata*/) {
  return Http::FilterMetadataStatus::Continue;
}

} // namespace PlatformBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
