#pragma once

#include "envoy/http/header_map.h"

#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authorization_handle() {
  static Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> handle(Http::CustomHeaders::get().Authorization);
  return handle;
}

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    pragma_handle() {
  static Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> handle(Http::CustomHeaders::get().Pragma);
  return handle;
}

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    request_cache_control_handle() {
  static Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
      handle(Http::CustomHeaders::get().CacheControl);
  return handle;
}

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    if_match_handle() {
  static Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
      handle(Http::CustomHeaders::get().IfMatch);
  return handle;
}

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    if_none_match_handle() {
  static Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
      handle(Http::CustomHeaders::get().IfNoneMatch);
  return handle;
}

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    if_modified_since_handle() {
  static Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
      handle(Http::CustomHeaders::get().IfModifiedSince);
  return handle;
}

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    if_unmodified_since_handle() {
  static Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
      handle(Http::CustomHeaders::get().IfUnmodifiedSince);
  return handle;
}

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    if_range_handle() {
  static Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
      handle(Http::CustomHeaders::get().IfRange);
  return handle;
}

// Response headers inline handles
inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    response_cache_control_handle() {
  static Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
      handle(Http::CustomHeaders::get().CacheControl);
  return handle;
}

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    last_modified_handle() {
  static Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
      handle(Http::CustomHeaders::get().LastModified);
  return handle;
}

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    etag_handle() {
  static Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
      handle(Http::CustomHeaders::get().Etag);
  return handle;
}

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    age_handle() {
  static Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders> handle(Http::Headers::get().Age);
  return handle;
}

inline Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    expires_handle() {
  static Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders> handle(Http::Headers::get().Expires);
  return handle;
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
