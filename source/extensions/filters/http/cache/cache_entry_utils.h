#pragma once

#include <memory>
#include <string>

#include "envoy/common/time.h"

#include "source/extensions/filters/http/cache/cache_headers_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// The metadata associated with a cached response.
// TODO(yosrym93): This could be changed to a proto if a need arises.
// If a cache was created with the current interface, then it was changed to a
// proto, all the cache entries will need to be invalidated.
struct ResponseMetadata {
  // The time at which a response was was most recently inserted, updated, or
  // validated in this cache. This represents "response_time" in the age header
  // calculations at: https://httpwg.org/specs/rfc7234.html#age.calculations
  Envoy::SystemTime response_time_;
};
using ResponseMetadataPtr = std::unique_ptr<ResponseMetadata>;

// Whether a given cache entry is good for the current request.
enum class CacheEntryStatus {
  // This entry is fresh, and an appropriate response to the request.
  Ok,
  // No usable entry was found. If this was generated for a cache entry, the
  // cache should delete that entry.
  Unusable,
  // This entry is stale, but appropriate for validating
  RequiresValidation,
  // This entry is fresh, and an appropriate basis for a 304 Not Modified
  // response.
  FoundNotModified,
};

absl::string_view cacheEntryStatusString(CacheEntryStatus s);
std::ostream& operator<<(std::ostream& os, CacheEntryStatus status);

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
