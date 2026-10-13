#pragma once

#include <memory>
#include <string>

#include "envoy/common/time.h"

#include "source/extensions/filters/http/cache_v2/cache_headers_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

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

// Whether a given cache entry is good for the current request.
enum class CacheEntryStatus {
  // This entry is fresh, and an appropriate response to the request.
  Hit,
  // The request was cacheable and was not already in the cache. This also means
  // the cache was populated by this request.
  Miss,
  // The entry was being inserted when this request was made - it's like a
  // hit, but streamed from the same request as the original "Miss", so still
  // potentially subject to upstream reset because the cache entry isn't fully
  // populated yet.
  Follower,
  // The request was not cacheable. All matching requests will go to the
  // upstream.
  Uncacheable,
  // This entry required validation, and validated successfully.
  Validated,
  // This entry required validation while another entry was already validating,
  // so it validated successfully without its own lookup.
  ValidatedFree,
  // This entry required validation, and did not validate.
  FailedValidation,
  // This entry is fresh, and an appropriate basis for a 304 Not Modified
  // response.
  FoundNotModified,
  // The cache lookup failed, e.g. because the cache was unreachable or an RPC
  // timed out. Mostly behaves the same as Uncacheable but may retry each time.
  LookupError,
  // The cache attempted to read from upstream for insert, but upstream reset.
  UpstreamReset,
};

absl::string_view cacheEntryStatusString(CacheEntryStatus s);
std::ostream& operator<<(std::ostream& os, CacheEntryStatus status);

// For an updateHeaders operation, new headers must be merged into existing headers
// for the cache entry. This helper function performs that merge correctly, i.e.
// - if a header appears in new_headers, prior values for that header are erased
//   from headers_to_update.
// - if a header appears more than once in new_headers, all new values are added
//   to headers_to_update.
// - headers that are not supposed to be updated during updateHeaders operations
//   (etag, content-length, content-range, vary) are ignored.
void applyHeaderUpdate(const Http::ResponseHeaderMap& new_headers,
                       Http::ResponseHeaderMap& headers_to_update);

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
