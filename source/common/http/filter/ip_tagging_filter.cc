#include "common/http/filter/ip_tagging_filter.h"

namespace Envoy {
namespace Http {

IpTaggingFilterStats IpTaggingFilterConfig::generateStats(const std::string& prefix,
                                                          Stats::Store& store) {
  std::string final_prefix = prefix + "ip_tagging.";
  return {ALL_IP_TAGGING_FILTER_STATS(POOL_COUNTER_PREFIX(store, final_prefix))};
}

void IpTaggingFilter::onDestroy() {}

FilterHeadersStatus IpTaggingFilter::decodeHeaders(HeaderMap&, bool) {
  return FilterHeadersStatus::Continue;
}

FilterDataStatus IpTaggingFilter::decodeData(Buffer::Instance&, bool) {
  return FilterDataStatus::Continue;
}

FilterTrailersStatus IpTaggingFilter::decodeTrailers(HeaderMap&) {
  return FilterTrailersStatus::Continue;
}

void IpTaggingFilter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks&) {}

} // Http
} // Envoy