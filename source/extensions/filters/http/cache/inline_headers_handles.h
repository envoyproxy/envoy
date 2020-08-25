#pragma once

#include "envoy/http/header_map.h"

#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// An internal header that represents the time at which a cached response was received by the cache.
// This header is added to cached responses and removed before they are served.
// This represents "response_time" in the age header calculations at:
// https://httpwg.org/specs/rfc7234.html#age.calculations
inline constexpr absl::string_view ResponseTimeHeader = "Envoy-Cache-Response-Time";

// Request headers inline handles
inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authorization_handle(Http::CustomHeaders::get().Authorization);

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    pragma_handle(Http::CustomHeaders::get().Pragma);

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    request_cache_control_handle(Http::CustomHeaders::get().CacheControl);

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    if_match_handle(Http::CustomHeaders::get().IfMatch);

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    if_none_match_handle(Http::CustomHeaders::get().IfNoneMatch);

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    if_modified_since_handle(Http::CustomHeaders::get().IfModifiedSince);

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    if_unmodified_since_handle(Http::CustomHeaders::get().IfUnmodifiedSince);

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    if_range_handle(Http::CustomHeaders::get().IfRange);

// Response headers inline handles
inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    response_cache_control_handle(Http::CustomHeaders::get().CacheControl);

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    last_modified_handle(Http::CustomHeaders::get().LastModified);

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    etag_handle(Http::CustomHeaders::get().Etag);

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    age_handle(Http::Headers::get().Age);

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    expires_handle(Http::Headers::get().Expires);

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    response_time_handle(Http::LowerCaseString{std::string(ResponseTimeHeader)});

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
