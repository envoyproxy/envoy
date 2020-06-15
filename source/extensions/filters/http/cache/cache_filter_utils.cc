#include "extensions/filters/http/cache/cache_filter_utils.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

bool CacheFilterUtils::isCacheableRequest(Http::RequestHeaderMap& headers) {
  const Http::HeaderEntry* method = headers.Method();
  const Http::HeaderEntry* forwarded_proto = headers.ForwardedProto();
  const Http::HeaderValues& header_values = Http::Headers::get();
  // TODO(toddmgreer): Also serve HEAD requests from cache.
  // TODO(toddmgreer): Check all the other cache-related headers.
  return method && forwarded_proto && headers.Path() && headers.Host() &&
         (method->value() == header_values.MethodValues.Get) &&
         (forwarded_proto->value() == header_values.SchemeValues.Http ||
          forwarded_proto->value() == header_values.SchemeValues.Https);
}

bool CacheFilterUtils::isCacheableResponse(Http::ResponseHeaderMap& headers) {
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