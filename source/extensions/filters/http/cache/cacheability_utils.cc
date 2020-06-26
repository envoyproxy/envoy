#include "extensions/filters/http/cache/cacheability_utils.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

bool CacheabilityUtils::isCacheableRequest(const Http::RequestHeaderMap& headers) {
  const absl::string_view method = headers.getMethodValue();
  const absl::string_view forwarded_proto = headers.getForwardedProtoValue();
  const Http::HeaderValues& header_values = Http::Headers::get();
  // TODO(toddmgreer): Also serve HEAD requests from cache.
  // TODO(toddmgreer): Check all the other cache-related headers.
  return headers.Path() && headers.Host() && !headers.Authorization() &&
         (method == header_values.MethodValues.Get) &&
         (forwarded_proto == header_values.SchemeValues.Http ||
          forwarded_proto == header_values.SchemeValues.Https);
}

bool CacheabilityUtils::isCacheableResponse(const Http::ResponseHeaderMap& headers,
                                            const RequestCacheControl& request_cache_control) {
  // As defined by:
  // https://tools.ietf.org/html/rfc7231#section-6.1,
  // https://tools.ietf.org/html/rfc7538#section-3,
  // https://tools.ietf.org/html/rfc7725#section-3
  // TODO: the list of cacheable status codes should be configurable
  const absl::flat_hash_set<absl::string_view> cacheable_status_codes_ = {
      "200", "203", "204", "206", "300", "301", "308", "404", "405", "410", "414", "451", "501"};

  ResponseCacheControl response_cache_control =
      CacheHeadersUtils::responseCacheControl(headers.getCacheControlValue());
  bool no_store = response_cache_control.no_store || request_cache_control.no_store;
  bool cacheable_status = cacheable_status_codes_.contains(headers.getStatusValue());

  // Only cache responses with explicit validation data, either:
  //    max-age or s-maxage cache-control directives with date header
  //    expires header
  // TODO: If the response has no date header inject date metadata
  bool has_validation_data = (headers.Date() && response_cache_control.max_age.has_value()) ||
                             headers.get(Http::Headers::get().Expires);

  return !no_store && cacheable_status && has_validation_data;
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy