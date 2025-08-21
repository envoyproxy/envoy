#pragma once

#include "envoy/http/header_map.h"

#include "source/common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

/**
 * CacheCustomHeaders provides access to registered cache-specific headers.
 */
struct CacheCustomHeaders {
  CacheCustomHeaders();

  // clang-format off
  const static Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> authorization();
  const static Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> pragma();
  const static Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> requestCacheControl();
  const static Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> ifMatch();
  const static Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> ifNoneMatch();
  const static Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> ifModifiedSince();
  const static Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> ifUnmodifiedSince();
  const static Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> ifRange();

  const static Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders> responseCacheControl();
  const static Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders> lastModified();
  const static Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders> etag();
  const static Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders> age();
  const static Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders> expires();
  // clang-format on

  // clang-format off
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> authorization_;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> pragma_;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> request_cache_control_;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> if_match_;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> if_none_match_;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> if_modified_since_;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> if_unmodified_since_;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> if_range_;

  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders> response_cache_control_;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders> last_modified_;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders> etag_;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders> age_;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders> expires_;
  // clang-format on

}; // Request headers inline handles

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
