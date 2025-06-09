#include "source/extensions/filters/http/proto_api_scrubber/filter.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

ProtoApiScrubberFilter::ProtoApiScrubberFilter(const ProtoApiScrubberFilterConfig&) {}

Envoy::Http::FilterHeadersStatus
ProtoApiScrubberFilter::decodeHeaders(Envoy::Http::RequestHeaderMap& headers, bool) {
  ENVOY_STREAM_LOG(debug, "Called method {} with headers={}", *decoder_callbacks_, __func__,
                   headers);
  return Envoy::Http::FilterHeadersStatus::Continue;
}

Envoy::Http::FilterDataStatus ProtoApiScrubberFilter::decodeData(Envoy::Buffer::Instance& data,
                                                                 bool end_stream) {
  ENVOY_STREAM_LOG(debug, "Called ProtoApiScrubber::decodeData: data size={} end_stream={}",
                   *decoder_callbacks_, data.length(), end_stream);
  return Envoy::Http::FilterDataStatus::Continue;
}

Envoy::Http::FilterHeadersStatus
ProtoApiScrubberFilter::encodeHeaders(Envoy::Http::ResponseHeaderMap& headers, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "Called method {} with headers={}. end_stream={}", *encoder_callbacks_,
                   __func__, headers, end_stream);
  return Envoy::Http::FilterHeadersStatus::Continue;
}

Envoy::Http::FilterDataStatus ProtoApiScrubberFilter::encodeData(Envoy::Buffer::Instance& data,
                                                                 bool end_stream) {
  ENVOY_STREAM_LOG(debug, "Called ProtoApiScrubber::encodeData: data size={} end_stream={}",
                   *encoder_callbacks_, data.length(), end_stream);
  return Envoy::Http::FilterDataStatus::Continue;
}
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
