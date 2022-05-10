#pragma once

#include "envoy/stream_info/filter_state.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

enum class CacheLookupStatus {
  // The CacheFilter couldn't determine the status of the request, probably
  // because of an internal error.
  Unknown,
  // The CacheFilter found a response in cache to serve.
  CacheHit,
  // The CacheFilter didn't find a response in cache.
  CacheMiss,
  // The CacheFilter found a stale response, and sent a validation request to
  // the upstream; the upstream responded with a 304 Not Modified.
  StaleHitWithSuccessfulValidation,
  // The CacheFilter found a stale response, and sent a validation request to
  // the upstream; the upstream responded with anything other than a 304 Not
  // Modified. (The CacheFilter will currently forward 5xx responses from the
  // upstream in this case, instead of sending the stale cache entry.)
  StaleHitWithFailedValidation,
  // The CacheFilter found a response in cache and served a 304 Not Modified.
  NotModifiedHit,
  // The request wasn't cacheable, and the CacheFilter didn't try to look it up
  // in cache.
  RequestNotCacheable,
  // The request was cancelled before the CacheFilter could determine a cache
  // status.
  RequestIncomplete,
  // The CacheFilter couldn't determine whether there was a response in cache,
  // e.g. because the cache was unreachable or the lookup RPC timed out.
  LookupError,
};

absl::string_view CacheLookupStatusToString(CacheLookupStatus status);

std::ostream& operator<<(std::ostream& os, const CacheLookupStatus& request_cache_status);

enum class CacheInsertStatus {
  // The CacheFilter attempted to insert a cache entry, and succeeded as far as
  // it knows. The filter doesn't wait for a final confirmation from the cache,
  // so the filter may still show this status for an insert that failed at e.g.
  // the last body chunk.
  InsertSucceeded,
  // The CacheFilter started an insert, but the HttpCache aborted it.
  InsertAbortedByCache,
  // The CacheFilter started an insert, but aborted it because the cache wasn't
  // ready as a body chunk came in.
  InsertAbortedCacheCongested,
  // The CacheFilter started an insert, but couldn't finish it because the
  // stream was closed before the response finished. Until the CacheFilter
  // supports caching response trailers, this will also be reported if it tries
  // to cache a response with
  // trailers.
  InsertAbortedResponseIncomplete,
  // The CacheFilter attempted to update the headers of an existing cache entry.
  // This doesn't record whether or not the update succeeded.
  HeaderUpdate,
  // The CacheFilter found a cache entry and didn't need to insert or update its
  // headers.
  NoInsertCacheHit,
  // The CacheFilter got an uncacheable request and didn't try to cache the
  // response.
  NoInsertRequestNotCacheable,
  // The CacheFilter got an uncacheable response and didn't cache it.
  NoInsertResponseNotCacheable,
  // The request was cancelled before the CacheFilter decided whether or not to
  // insert the response.
  NoInsertRequestIncomplete,
  // The CacheFilter got a 304 validation response not matching the etag strong
  // validator of our cached entry. The cached entry needs to be removed
  NoInsertResponseValidatorsMismatch,
  // The CacheFilter got a 304 validation response not matching the vary header
  // fields. The cached variant set needs to be removed.
  NoInsertResponseVaryMismatch,
  // The CacheFilter got a 304 validation response, but the vary header is now
  // disallowed by the vary allow list
  NoInsertResponseVaryDisallowed,
  // The CacheFilter couldn't determine whether the request was in cache and
  // didn't try to insert it.
  NoInsertLookupError,
};

absl::string_view CacheInsertStatusToString(CacheInsertStatus status);

std::ostream& operator<<(std::ostream& os, const CacheInsertStatus& cache_insert_status);

// Cache-related information about a request, to be used for logging and stats.
class CacheFilterLoggingInfo : public Envoy::StreamInfo::FilterState::Object {
public:
  static constexpr absl::string_view Key =
      "io.envoyproxy.extensions.filters.http.cache.CacheFilterLoggingInfo";

  CacheFilterLoggingInfo(CacheLookupStatus cache_lookup_status,
                         CacheInsertStatus cache_insert_status)
      : cache_lookup_status_(cache_lookup_status), cache_insert_status_(cache_insert_status) {}

  CacheLookupStatus cache_lookup_status() const { return cache_lookup_status_; }

  CacheInsertStatus cache_insert_status() const { return cache_insert_status_; }

private:
  const CacheLookupStatus cache_lookup_status_;
  const CacheInsertStatus cache_insert_status_;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
