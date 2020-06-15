#pragma once

#include "common/common/utility.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
class CacheFilterUtils {
public:
  // Checks if a request can be served from cache
  static bool isCacheableRequest(const Http::RequestHeaderMap& headers);

  // Checks if a response can be stored in cache
  static bool isCacheableResponse(const Http::ResponseHeaderMap& headers);
};
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy