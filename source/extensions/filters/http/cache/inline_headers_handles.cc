#include "extensions/filters/http/cache/inline_headers_handles.h"

#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authorization_handle(Http::CustomHeaders::get().Authorization);

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    pragma_handle(Http::CustomHeaders::get().Pragma);

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    request_cache_control_handle(Http::CustomHeaders::get().CacheControl);

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    if_match_handle(Http::CustomHeaders::get().IfMatch);

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    if_none_match_handle(Http::CustomHeaders::get().IfNoneMatch);

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    if_modified_since_handle(Http::CustomHeaders::get().IfModifiedSince);

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    if_unmodified_since_handle(Http::CustomHeaders::get().IfUnmodifiedSince);

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    if_range_handle(Http::CustomHeaders::get().IfRange);

// Response headers inline handles
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    response_cache_control_handle(Http::CustomHeaders::get().CacheControl);

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    last_modified_handle(Http::CustomHeaders::get().LastModified);

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    etag_handle(Http::CustomHeaders::get().Etag);

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
