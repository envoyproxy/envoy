#include "source/extensions/filters/http/cache/cacheability_utils.h"

#include "envoy/http/header_map.h"

#include "source/common/common/macros.h"
#include "source/common/common/utility.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/cache/cache_custom_headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

namespace {
const absl::flat_hash_set<absl::string_view>& cacheableStatusCodes() {
  // As defined by:
  // https://tools.ietf.org/html/rfc7231#section-6.1,
  // https://tools.ietf.org/html/rfc7538#section-3,
  // https://tools.ietf.org/html/rfc7725#section-3
  // TODO(yosrym93): the list of cacheable status codes should be configurable.
  CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<absl::string_view>, "200", "203", "204", "206", "300",
                         "301", "308", "404", "405", "410", "414", "451", "501");
}

const std::vector<const Http::LowerCaseString*>& conditionalHeaders() {
  // As defined by: https://httpwg.org/specs/rfc7232.html#preconditions.
  CONSTRUCT_ON_FIRST_USE(
      std::vector<const Http::LowerCaseString*>, &Http::CustomHeaders::get().IfNoneMatch,
      &Http::CustomHeaders::get().IfModifiedSince, &Http::CustomHeaders::get().IfRange);
}
} // namespace

bool CacheabilityUtils::canServeRequestFromCache(const Http::RequestHeaderMap& headers) {
  const absl::string_view method = headers.getMethodValue();
  const Http::HeaderValues& header_values = Http::Headers::get();

  // Check if the request contains any conditional headers other than if-unmodified-since
  // or if-match.
  // For now, requests with conditional headers bypass the CacheFilter.
  // This behavior does not cause any incorrect results, but may reduce the cache effectiveness.
  // If needed to be handled properly refer to:
  // https://httpwg.org/specs/rfc7234.html#validation.received
  // if-unmodified-since and if-match are ignored, as the spec explicitly says these
  // header fields can be ignored by caches and intermediaries.
  for (auto conditional_header : conditionalHeaders()) {
    if (!headers.get(*conditional_header).empty()) {
      return false;
    }
  }

  // TODO(toddmgreer): Also serve HEAD requests from cache.
  // Cache-related headers are checked in HttpCache::LookupRequest.
  return headers.Path() && headers.Host() &&
         !headers.getInline(CacheCustomHeaders::authorization()) &&
         (method == header_values.MethodValues.Get || method == header_values.MethodValues.Head) &&
         Http::Utility::schemeIsValid(headers.getSchemeValue());
}

bool CacheabilityUtils::isCacheableResponse(const Http::ResponseHeaderMap& headers,
                                            const VaryAllowList& vary_allow_list) {
  absl::string_view cache_control =
      headers.getInlineValue(CacheCustomHeaders::responseCacheControl());
  ResponseCacheControl response_cache_control(cache_control);

  // Only cache responses with enough data to calculate freshness lifetime as per:
  // https://httpwg.org/specs/rfc7234.html#calculating.freshness.lifetime.
  // Either:
  //    "no-cache" cache-control directive (requires revalidation anyway).
  //    "max-age" or "s-maxage" cache-control directives.
  //    Both "Expires" and "Date" headers.
  const bool has_validation_data =
      response_cache_control.must_validate_ || response_cache_control.max_age_.has_value() ||
      (headers.Date() && headers.getInline(CacheCustomHeaders::expires()));

  return !response_cache_control.no_store_ &&
         cacheableStatusCodes().contains((headers.getStatusValue())) && has_validation_data &&
         vary_allow_list.allowsHeaders(headers);
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
