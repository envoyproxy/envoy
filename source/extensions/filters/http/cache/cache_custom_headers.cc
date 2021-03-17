#include "extensions/filters/http/cache/cache_custom_headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

static CacheCustomHeaders custom_headers;
CacheCustomHeaders& CacheCustomHeaders::get() { return custom_headers; }

CacheCustomHeaders::CacheCustomHeaders()
    : authorization(Http::CustomHeaders::get().Authorization),
      pragma(Http::CustomHeaders::get().Pragma),
      request_cache_control(Http::CustomHeaders::get().CacheControl),
      if_match(Http::CustomHeaders::get().IfMatch),
      if_none_match(Http::CustomHeaders::get().IfNoneMatch),
      if_modified_since(Http::CustomHeaders::get().IfModifiedSince),
      if_unmodified_since(Http::CustomHeaders::get().IfUnmodifiedSince),
      if_range(Http::CustomHeaders::get().IfRange),
      response_cache_control(Http::CustomHeaders::get().CacheControl),
      last_modified(Http::CustomHeaders::get().LastModified), etag(Http::CustomHeaders::get().Etag),
      age(Http::CustomHeaders::get().Age), expires(Http::CustomHeaders::get().Expires) {}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
