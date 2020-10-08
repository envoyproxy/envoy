#include "extensions/filters/http/mutation/mutation.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mutation {

void Filter::onDestroy() {}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks&) {}

Http::FilterHeadersStatus Filter::encode100ContinueHeaders(Http::ResponseHeaderMap&) {
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap&, bool) {
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterMetadataStatus Filter::encodeMetadata(Http::MetadataMap&) {
  return Http::FilterMetadataStatus::Continue;
}

void Filter::setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks&) {}

} // namespace Mutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy