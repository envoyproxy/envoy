#pragma once

#include "source/common/common/utility.h"
#include "source/common/http/headers.h"
#include "source/extensions/filters/http/cache_v2/cache_headers_utils.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace CacheabilityUtils {
// Checks if a request can be served from cache.
// This does not depend on cache-control headers as
// request cache-control headers only decide whether
// validation is required and whether the response can be cached.
absl::Status canServeRequestFromCache(const Http::RequestHeaderMap& headers);

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
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
