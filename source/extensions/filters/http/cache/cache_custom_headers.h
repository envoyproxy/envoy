#pragma once

#include "envoy/http/header_map.h"

#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

struct CacheCustomHeaders {
  static CacheCustomHeaders& get();
  CacheCustomHeaders();

  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
      authorization;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> pragma;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
      request_cache_control;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> if_match;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
      if_none_match;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
      if_modified_since;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
      if_unmodified_since;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders> if_range;

  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
      response_cache_control;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
      last_modified;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders> etag;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders> age;
  Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders> expires;

}; // Request headers inline handles

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
