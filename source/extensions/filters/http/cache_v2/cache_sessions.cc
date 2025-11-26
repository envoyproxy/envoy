#include "source/extensions/filters/http/cache_v2/cache_sessions.h"

#include <limits>

#include "source/common/http/utility.h"
#include "source/extensions/filters/http/cache_v2/cache_custom_headers.h"
#include "source/extensions/filters/http/cache_v2/cache_headers_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

ActiveLookupRequest::ActiveLookupRequest(
    const Http::RequestHeaderMap& request_headers,
    UpstreamRequestFactoryPtr upstream_request_factory, absl::string_view cluster_name,
    Event::Dispatcher& dispatcher, SystemTime timestamp,
    const std::shared_ptr<const CacheableResponseChecker> cacheable_response_checker,
    const std::shared_ptr<const CacheFilterStatsProvider> stats_provider,
    bool ignore_request_cache_control_header)
    : upstream_request_factory_(std::move(upstream_request_factory)), dispatcher_(dispatcher),
      key_(CacheHeadersUtils::makeKey(request_headers, cluster_name)),
      request_headers_(Http::createHeaderMap<Http::RequestHeaderMapImpl>(request_headers)),
      cacheable_response_checker_(std::move(cacheable_response_checker)),
      stats_provider_(std::move(stats_provider)), timestamp_(timestamp) {
  if (!ignore_request_cache_control_header) {
    initializeRequestCacheControl(request_headers);
  }
}

absl::optional<std::vector<RawByteRange>> ActiveLookupRequest::parseRange() const {
  auto range_header = RangeUtils::getRangeHeader(*request_headers_);
  if (!range_header) {
    return absl::nullopt;
  }
  return RangeUtils::parseRangeHeader(range_header.value(), 1);
}

bool ActiveLookupRequest::isRangeRequest() const {
  return RangeUtils::getRangeHeader(*request_headers_).has_value();
}

void ActiveLookupRequest::initializeRequestCacheControl(
    const Http::RequestHeaderMap& request_headers) {
  const absl::string_view cache_control =
      request_headers.getInlineValue(CacheCustomHeaders::requestCacheControl());

  if (!cache_control.empty()) {
    request_cache_control_ = RequestCacheControl(cache_control);
  } else {
    const absl::string_view pragma = request_headers.getInlineValue(CacheCustomHeaders::pragma());
    // According to: https://httpwg.org/specs/rfc7234.html#header.pragma,
    // when Cache-Control header is missing, "Pragma:no-cache" is equivalent to
    // "Cache-Control:no-cache". Any other directives are ignored.
    request_cache_control_.must_validate_ = RequestCacheControl(pragma).must_validate_;
  }
}

bool ActiveLookupRequest::requiresValidation(const Http::ResponseHeaderMap& response_headers,
                                             SystemTime::duration response_age) const {
  // TODO(yosrym93): Store parsed response cache-control in cache instead of parsing it on every
  // lookup.
  const absl::string_view cache_control =
      response_headers.getInlineValue(CacheCustomHeaders::responseCacheControl());
  const ResponseCacheControl response_cache_control(cache_control);

  const bool request_max_age_exceeded = request_cache_control_.max_age_.has_value() &&
                                        request_cache_control_.max_age_.value() < response_age;
  if (response_cache_control.must_validate_ || request_cache_control_.must_validate_ ||
      request_max_age_exceeded) {
    // Either the request or response explicitly require validation, or a request max-age
    // requirement is not satisfied.
    return true;
  }

  // CacheabilityUtils::isCacheableResponse(..) guarantees that any cached response satisfies this.
  ASSERT(response_cache_control.max_age_.has_value() ||
             (response_headers.getInline(CacheCustomHeaders::expires()) && response_headers.Date()),
         "Cache entry does not have valid expiration data.");

  SystemTime::duration freshness_lifetime;
  if (response_cache_control.max_age_.has_value()) {
    freshness_lifetime = response_cache_control.max_age_.value();
  } else {
    const SystemTime expires_value =
        CacheHeadersUtils::httpTime(response_headers.getInline(CacheCustomHeaders::expires()));
    const SystemTime date_value = CacheHeadersUtils::httpTime(response_headers.Date());
    freshness_lifetime = expires_value - date_value;
  }

  if (response_age > freshness_lifetime) {
    // Response is stale, requires validation if
    // the response does not allow being served stale,
    // or the request max-stale directive does not allow it.
    const bool allowed_by_max_stale =
        request_cache_control_.max_stale_.has_value() &&
        request_cache_control_.max_stale_.value() > response_age - freshness_lifetime;
    return response_cache_control.no_stale_ || !allowed_by_max_stale;
  } else {
    // Response is fresh, requires validation only if there is an unsatisfied min-fresh requirement.
    const bool min_fresh_unsatisfied =
        request_cache_control_.min_fresh_.has_value() &&
        request_cache_control_.min_fresh_.value() > freshness_lifetime - response_age;
    return min_fresh_unsatisfied;
  }
}

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
