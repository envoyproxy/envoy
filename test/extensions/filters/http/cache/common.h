#pragma once

#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

std::ostream& operator<<(std::ostream& os, const RequestCacheControl& request_cache_control);

std::ostream& operator<<(std::ostream& os, const ResponseCacheControl& response_cache_control);

std::ostream& operator<<(std::ostream& os, const AdjustedByteRange& range);

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
