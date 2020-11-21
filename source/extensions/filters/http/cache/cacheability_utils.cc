#include "extensions/filters/http/cache/cacheability_utils.h"

#include "envoy/http/header_map.h"

#include "common/common/macros.h"
#include "common/common/utility.h"

#include "extensions/filters/http/cache/inline_headers_handles.h"

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
      std::vector<const Http::LowerCaseString*>, &Http::CustomHeaders::get().IfMatch,
      &Http::CustomHeaders::get().IfNoneMatch, &Http::CustomHeaders::get().IfModifiedSince,
      &Http::CustomHeaders::get().IfUnmodifiedSince, &Http::CustomHeaders::get().IfRange);
}
} // namespace

bool CacheabilityUtils::isCacheableRequest(const Http::RequestHeaderMap& headers) {
  const absl::string_view method = headers.getMethodValue();
  const absl::string_view forwarded_proto = headers.getForwardedProtoValue();
  const Http::HeaderValues& header_values = Http::Headers::get();

  // Check if the request contains any conditional headers.
  // For now, requests with conditional headers bypass the CacheFilter.
  // This behavior does not cause any incorrect results, but may reduce the cache effectiveness.
  // If needed to be handled properly refer to:
  // https://httpwg.org/specs/rfc7234.html#validation.received
  for (auto conditional_header : conditionalHeaders()) {
    if (!headers.get(*conditional_header).empty()) {
      return false;
    }
  }

  // TODO(toddmgreer): Also serve HEAD requests from cache.
  // Cache-related headers are checked in HttpCache::LookupRequest.
  return headers.Path() && headers.Host() && !headers.getInline(authorization_handle.handle()) &&
         (method == header_values.MethodValues.Get) &&
         (forwarded_proto == header_values.SchemeValues.Http ||
          forwarded_proto == header_values.SchemeValues.Https);
}

bool CacheabilityUtils::isCacheableResponse(const Http::ResponseHeaderMap& headers,
                                            const VaryHeader& vary_allow_list) {
  absl::string_view cache_control = headers.getInlineValue(response_cache_control_handle.handle());
  ResponseCacheControl response_cache_control(cache_control);

  // Only cache responses with enough data to calculate freshness lifetime as per:
  // https://httpwg.org/specs/rfc7234.html#calculating.freshness.lifetime.
  // Either:
  //    "no-cache" cache-control directive (requires revalidation anyway).
  //    "max-age" or "s-maxage" cache-control directives.
  //    Both "Expires" and "Date" headers.
  const bool has_validation_data = response_cache_control.must_validate_ ||
                                   response_cache_control.max_age_.has_value() ||
                                   (headers.Date() && headers.getInline(expires_handle.handle()));

  return !response_cache_control.no_store_ &&
         cacheableStatusCodes().contains((headers.getStatusValue())) && has_validation_data &&
         vary_allow_list.isAllowed(headers);
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
