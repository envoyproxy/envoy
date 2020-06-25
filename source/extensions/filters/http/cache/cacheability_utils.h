#pragma once

#include "common/common/utility.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
class CacheabilityUtils {
public:
  // Checks if a request can be served from cache,
  // this does not depend on cache-control headers as
  // request cache-control headers only decide whether
  // validation is required and whether the response can be cached
  static bool isCacheableRequest(const Http::RequestHeaderMap& headers);

  // Checks if a response can be stored in cache
  // Should follow RFC7234 Section 3 at https://httpwg.org/specs/rfc7234.html#response.cacheability
  static bool isCacheableResponse(const Http::ResponseHeaderMap& headers);
};
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy