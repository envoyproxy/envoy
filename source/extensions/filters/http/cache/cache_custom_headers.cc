#include "source/extensions/filters/http/cache/cache_custom_headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

using RequestHeaderHandle = Http::CustomInlineHeaderRegistry::Handle<
    Http::CustomInlineHeaderRegistry::Type::RequestHeaders>;
using ResponseHeaderHandle = Http::CustomInlineHeaderRegistry::Handle<
    Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>;

static CacheCustomHeaders custom_headers;

// clang-format off
const RequestHeaderHandle CacheCustomHeaders::authorization() { return custom_headers.authorization_.handle(); }
const RequestHeaderHandle CacheCustomHeaders::pragma() { return custom_headers.pragma_.handle(); }
const RequestHeaderHandle CacheCustomHeaders::requestCacheControl() { return custom_headers.request_cache_control_.handle(); }
const RequestHeaderHandle CacheCustomHeaders::ifMatch() { return custom_headers.if_match_.handle(); }
const RequestHeaderHandle CacheCustomHeaders::ifNoneMatch() { return custom_headers.if_none_match_.handle(); }
const RequestHeaderHandle CacheCustomHeaders::ifModifiedSince() { return custom_headers.if_modified_since_.handle(); }
const RequestHeaderHandle CacheCustomHeaders::ifUnmodifiedSince() { return custom_headers.if_unmodified_since_.handle(); }
const RequestHeaderHandle CacheCustomHeaders::ifRange() { return custom_headers.if_range_.handle(); }

const ResponseHeaderHandle CacheCustomHeaders::responseCacheControl() { return custom_headers.response_cache_control_.handle(); }
const ResponseHeaderHandle CacheCustomHeaders::lastModified() { return custom_headers.last_modified_.handle(); }
const ResponseHeaderHandle CacheCustomHeaders::age() { return custom_headers.age_.handle(); }
const ResponseHeaderHandle CacheCustomHeaders::etag() { return custom_headers.etag_.handle(); }
const ResponseHeaderHandle CacheCustomHeaders::expires() { return custom_headers.expires_.handle(); }
// clang-format on

// clang-format off
CacheCustomHeaders::CacheCustomHeaders()
    : authorization_(Http::CustomHeaders::get().Authorization),
      pragma_(Http::CustomHeaders::get().Pragma),
      request_cache_control_(Http::CustomHeaders::get().CacheControl),
      if_match_(Http::CustomHeaders::get().IfMatch),
      if_none_match_(Http::CustomHeaders::get().IfNoneMatch),
      if_modified_since_(Http::CustomHeaders::get().IfModifiedSince),
      if_unmodified_since_(Http::CustomHeaders::get().IfUnmodifiedSince),
      if_range_(Http::CustomHeaders::get().IfRange),
      response_cache_control_(Http::CustomHeaders::get().CacheControl),
      last_modified_(Http::CustomHeaders::get().LastModified),
      etag_(Http::CustomHeaders::get().Etag),
      age_(Http::CustomHeaders::get().Age),
      expires_(Http::CustomHeaders::get().Expires) {}
// clang-format on

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
