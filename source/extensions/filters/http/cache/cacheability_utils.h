#pragma once

#include "source/common/common/utility.h"
#include "source/common/http/headers.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace CacheabilityUtils {
// Checks if a request can be served from cache.
// This does not depend on cache-control headers as
// request cache-control headers only decide whether
// validation is required and whether the response can be cached.
bool canServeRequestFromCache(const Http::RequestHeaderMap& headers);

// Checks if a response can be stored in cache.
// Note that if a request is not cacheable according to 'canServeRequestFromCache'
// then its response is also not cacheable.
// Therefore, canServeRequestFromCache, isCacheableResponse and
// CacheFilter::request_allows_inserts_ together should cover
// https://httpwg.org/specs/rfc7234.html#response.cacheability. Head requests are not
// cacheable. However, this function is never called for head requests.
bool isCacheableResponse(const Http::ResponseHeaderMap& headers,
                         const VaryAllowList& vary_allow_list);
} // namespace CacheabilityUtils
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
