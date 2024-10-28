#include "source/extensions/filters/http/dynamic_modules/filter.h"

#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

void DynamicModuleHttpFilter::onStreamComplete() {}

void DynamicModuleHttpFilter::onDestroy(){};

FilterHeadersStatus DynamicModuleHttpFilter::decodeHeaders(RequestHeaderMap&, bool) {
  return FilterHeadersStatus::Continue;
};

FilterDataStatus DynamicModuleHttpFilter::decodeData(Buffer::Instance&, bool) {
  return FilterDataStatus::Continue;
};

FilterTrailersStatus DynamicModuleHttpFilter::decodeTrailers(RequestTrailerMap&) {
  return FilterTrailersStatus::Continue;
}

FilterMetadataStatus DynamicModuleHttpFilter::decodeMetadata(MetadataMap&) {
  return FilterMetadataStatus::Continue;
}

void DynamicModuleHttpFilter::decodeComplete() {}

Filter1xxHeadersStatus DynamicModuleHttpFilter::encode1xxHeaders(ResponseHeaderMap&) {
  return Filter1xxHeadersStatus::Continue;
}

FilterHeadersStatus DynamicModuleHttpFilter::encodeHeaders(ResponseHeaderMap&, bool) {
  return FilterHeadersStatus::Continue;
};

FilterDataStatus DynamicModuleHttpFilter::encodeData(Buffer::Instance&, bool) {
  return FilterDataStatus::Continue;
};

FilterTrailersStatus DynamicModuleHttpFilter::encodeTrailers(ResponseTrailerMap&) {
  return FilterTrailersStatus::Continue;
};

FilterMetadataStatus DynamicModuleHttpFilter::encodeMetadata(MetadataMap&) {
  return FilterMetadataStatus::Continue;
}

void DynamicModuleHttpFilter::encodeComplete(){};

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
