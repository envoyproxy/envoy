#include "extensions/filters/http/cache/inline_headers_handles.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

static CacheCustomHeaders custom_headers;
CacheCustomHeaders& CacheCustomHeaders::get() { return custom_headers; }

CacheCustomHeaders::CacheCustomHeaders()
    : authorization_handle(Http::CustomHeaders::get().Authorization),
      pragma_handle(Http::CustomHeaders::get().Pragma),
      request_cache_control_handle(Http::CustomHeaders::get().CacheControl),
      if_match_handle(Http::CustomHeaders::get().IfMatch),
      if_none_match_handle(Http::CustomHeaders::get().IfNoneMatch),
      if_modified_since_handle(Http::CustomHeaders::get().IfModifiedSince),
      if_unmodified_since_handle(Http::CustomHeaders::get().IfUnmodifiedSince),
      if_range_handle(Http::CustomHeaders::get().IfRange),
      response_cache_control_handle(Http::CustomHeaders::get().CacheControl),
      last_modified_handle(Http::CustomHeaders::get().LastModified),
      etag_handle(Http::CustomHeaders::get().Etag), age_handle(Http::CustomHeaders::get().Age),
      expires_handle(Http::CustomHeaders::get().Expires) {}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
