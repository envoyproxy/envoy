#include "common/http/filter/ip_tagging_filter.h"

#include "common/http/headers.h"
#include "common/http/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Http {

IpTaggingFilter::IpTaggingFilter(IpTaggingFilterConfigSharedPtr config) : config_(config) {}

IpTaggingFilter::~IpTaggingFilter() {}

void IpTaggingFilter::onDestroy() {}

FilterHeadersStatus IpTaggingFilter::decodeHeaders(HeaderMap& headers, bool) {
  const bool is_internal_request =
      headers.EnvoyInternalRequest() && (headers.EnvoyInternalRequest()->value() ==
                                         Headers::get().EnvoyInternalRequestValues.True.c_str());

  if ((is_internal_request && config_->requestType() == FilterRequestType::EXTERNAL) ||
      (!is_internal_request && config_->requestType() == FilterRequestType::INTERNAL) ||
      !config_->runtime().snapshot().featureEnabled("ip_tagging.http_filter_enabled", 100)) {
    return FilterHeadersStatus::Continue;
  }

  std::vector<std::string> tags =
      config_->trie().getTags(callbacks_->requestInfo().downstreamRemoteAddress());

  if (!tags.empty()) {
    const std::string tags_join = absl::StrJoin(tags, ",");
    Utility::appendToHeader(headers.insertEnvoyIpTags().value(), tags_join);

    // For a large number(ex > 1000) of tags, stats cardinality will be an issue.
    // If there are use cases with a large set of tags, a way to opt into these stats
    // should be exposed and other observability options like logging tags need to be implemented.
    for (const std::string& tag : tags) {
      config_->scope().counter(fmt::format("{}{}.hit", config_->statsPrefix(), tag)).inc();
    }
  } else {
    config_->scope().counter(fmt::format("{}no_hit", config_->statsPrefix())).inc();
  }
  config_->scope().counter(fmt::format("{}total", config_->statsPrefix())).inc();
  return FilterHeadersStatus::Continue;
}

FilterDataStatus IpTaggingFilter::decodeData(Buffer::Instance&, bool) {
  return FilterDataStatus::Continue;
}

FilterTrailersStatus IpTaggingFilter::decodeTrailers(HeaderMap&) {
  return FilterTrailersStatus::Continue;
}

void IpTaggingFilter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // namespace Http
} // namespace Envoy
