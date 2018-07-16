#include "extensions/filters/http/ip_tagging/ip_tagging_filter.h"

#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

IpTaggingFilter::IpTaggingFilter(IpTaggingFilterConfigSharedPtr config) : config_(config) {}

IpTaggingFilter::~IpTaggingFilter() {}

void IpTaggingFilter::onDestroy() {}

Http::FilterHeadersStatus IpTaggingFilter::decodeHeaders(Http::HeaderMap& headers, bool) {
  const bool is_internal_request = headers.EnvoyInternalRequest() &&
                                   (headers.EnvoyInternalRequest()->value() ==
                                    Http::Headers::get().EnvoyInternalRequestValues.True.c_str());

  if ((is_internal_request && config_->requestType() == FilterRequestType::EXTERNAL) ||
      (!is_internal_request && config_->requestType() == FilterRequestType::INTERNAL) ||
      !config_->runtime().snapshot().featureEnabled("ip_tagging.http_filter_enabled", 100)) {
    return Http::FilterHeadersStatus::Continue;
  }

  std::vector<std::string> tags =
      config_->trie().getData(callbacks_->requestInfo().downstreamRemoteAddress());

  if (!tags.empty()) {
    const std::string tags_join = absl::StrJoin(tags, ",");
    Http::HeaderMapImpl::appendToHeader(headers.insertEnvoyIpTags().value(), tags_join);

    // We must clear the route cache or else we can't match on x-envoy-ip-tags.
    // TODO(rgs): this should either be configurable, because it's expensive, or optimized.
    callbacks_->clearRouteCache();

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
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus IpTaggingFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus IpTaggingFilter::decodeTrailers(Http::HeaderMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void IpTaggingFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
