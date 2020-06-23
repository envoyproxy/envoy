#include "extensions/filters/http/cache/cache_filter_utils.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

bool CacheFilterUtils::isCacheableRequest(const Http::RequestHeaderMap& headers) {
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

bool CacheFilterUtils::isCacheableResponse(const Http::ResponseHeaderMap& headers) {
  const absl::string_view cache_control = headers.getCacheControlValue();
  // TODO(toddmgreer): fully check for cacheability. See for example
  // https://github.com/apache/incubator-pagespeed-mod/blob/master/pagespeed/kernel/http/caching_headers.h.
  return !StringUtil::caseFindToken(cache_control, ",",
                                    Http::Headers::get().CacheControlValues.Private);
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy