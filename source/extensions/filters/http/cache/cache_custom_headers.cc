#include "extensions/filters/http/cache/cache_custom_headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

using RequestHeaderHandle = Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>;
using ResponseHeaderHandle = Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>;

static CacheCustomHeaders custom_headers;

// clang-format off
const RequestHeaderHandle CacheCustomHeaders::Authorization() { return custom_headers.authorization.handle(); }
const RequestHeaderHandle CacheCustomHeaders::Pragma() { return custom_headers.pragma.handle(); }
const RequestHeaderHandle CacheCustomHeaders::RequestCacheControl() { return custom_headers.request_cache_control.handle(); }
const RequestHeaderHandle CacheCustomHeaders::IfMatch() { return custom_headers.if_match.handle(); }
const RequestHeaderHandle CacheCustomHeaders::IfNoneMatch() { return custom_headers.if_none_match.handle(); }
const RequestHeaderHandle CacheCustomHeaders::IfModifiedSince() { return custom_headers.if_modified_since.handle(); }
const RequestHeaderHandle CacheCustomHeaders::IfUnmodifiedSince() { return custom_headers.if_unmodified_since.handle(); }
const RequestHeaderHandle CacheCustomHeaders::IfRange() { return custom_headers.if_range.handle(); }

const ResponseHeaderHandle CacheCustomHeaders::ResponseCacheControl() { return custom_headers.response_cache_control.handle(); }
const ResponseHeaderHandle CacheCustomHeaders::LastModified() { return custom_headers.last_modified.handle(); }
const ResponseHeaderHandle CacheCustomHeaders::Age() { return custom_headers.age.handle(); }
const ResponseHeaderHandle CacheCustomHeaders::Etag() { return custom_headers.etag.handle(); }
const ResponseHeaderHandle CacheCustomHeaders::Expires() { return custom_headers.expires.handle(); }
// clang-format on

// clang-format off
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
      last_modified(Http::CustomHeaders::get().LastModified),
      etag(Http::CustomHeaders::get().Etag),
      age(Http::CustomHeaders::get().Age),
      expires(Http::CustomHeaders::get().Expires) {}
// clang-format on


} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
