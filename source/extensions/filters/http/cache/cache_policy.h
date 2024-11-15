#pragma once

#include <ostream>

#include "envoy/http/header_map.h"
#include "envoy/stream_info/filter_state.h"

#include "source/extensions/filters/http/cache/http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

/**
 * Contains information about whether the cache entry is usable.
 */
struct CacheEntryUsability {
  /**
   * Whether the cache entry is usable, additional checks are required to be usable, or unusable.
   */
  CacheEntryStatus status = CacheEntryStatus::Unusable;
  /**
   * Value to be put in the Age header for cache responses.
   */
  Seconds age = Seconds::max();
  /**
   * Remaining freshness lifetime--how long from now until the response is stale. (If the response
   * is already stale, `ttl` should be negative.)
   */
  Seconds ttl = Seconds::max();

  friend bool operator==(const CacheEntryUsability& a, const CacheEntryUsability& b) {
    return std::tie(a.status, a.age, a.ttl) == std::tie(b.status, b.age, b.ttl);
  }

  friend bool operator!=(const CacheEntryUsability& a, const CacheEntryUsability& b) {
    return !(a == b);
  }
};

enum class RequestCacheability {
  // This request is eligible for serving from cache, and for having its response stored.
  Cacheable,
  // Don't respond to this request from cache, or store its response into cache.
  Bypass,
  // This request is eligible for serving from cache, but its response
  // must not be stored. Consider the following sequence:
  // - Request 1: "curl http://example.com/ -H 'cache-control: no-store'"
  // - `requestCacheability` returns `NoStore`.
  // - CacheFilter finds nothing in cache, so request 1 is proxied upstream.
  // - Origin responds with a cacheable response 1.
  // - CacheFilter does not store response 1 into cache.
  // - Request 2: "curl http://example.com/"
  // - `requestCacheability` returns `Cacheable`.
  // - CacheFilter finds nothing in cache, so request 2 is proxied upstream.
  // - Origin responds with a cacheable response 2.
  // - CacheFilter stores response 2 into cache.
  // - Request 3: "curl http://example.com/ -H 'cache-control: no-store'"
  // - `requestCacheability` returns `NoStore`.
  // - CacheFilter looks in cache and finds response 2, which matches.
  // - CacheFilter serves response 2 from cache.
  // To summarize, all 3 requests were eligible for serving from cache (though only request 3 found
  // a match to serve), but only request 2 was allowed to have its response stored into cache.
  NoStore,
};

inline std::ostream& operator<<(std::ostream& os, RequestCacheability cacheability) {
  switch (cacheability) {
    using enum RequestCacheability;
  case Cacheable:
    return os << "Cacheable";
  case Bypass:
    return os << "Bypass";
  case NoStore:
    return os << "NoStore";
  }
}

enum class ResponseCacheability {
  // Don't store this response in cache.
  DoNotStore,
  // Store the full response in cache.
  StoreFullResponse,
  // Store a cache entry indicating that the response was uncacheable, and that future responses are
  // likely to be uncacheable. (CacheFilter and/or HttpCache implementations will treat such entries
  // as cache misses, but may enable optimizations based on expecting uncacheable responses. If a
  // future response is cacheable, it will overwrite this "uncacheable" entry.)
  MarkUncacheable,
};

inline std::ostream& operator<<(std::ostream& os, ResponseCacheability cacheability) {
  switch (cacheability) {
    using enum ResponseCacheability;
  case DoNotStore:
    return os << "DoNotStore";
  case StoreFullResponse:
    return os << "StoreFullResponse";
  case MarkUncacheable:
    return os << "MarkUncacheable";
  }
}

// Create cache key, calculate cache content freshness and
// response cacheability. This can be a straight RFC compliant implementation
// but can also be used to implement deployment specific cache policies.
//
// NOT YET IMPLEMENTED: To make CacheFilter use a custom cache policy, store a mutable CachePolicy
// in FilterState before CacheFilter::decodeHeaders is called.
class CachePolicy : public StreamInfo::FilterState::Object {
public:
  // For use in FilterState.
  static constexpr absl::string_view Name =
      "io.envoyproxy.extensions.filters.http.cache.cache_policy";

  virtual ~CachePolicy() = default;

  /**
   * Calculates the lookup key for storing the entry in the cache.
   * @param request_headers - headers from the request the CacheFilter is currently processing.
   */
  virtual Key cacheKey(const Http::RequestHeaderMap& request_headers) PURE;

  /**
   * Determines whether the request is eligible for serving from cache and/or having its response
   * stored in cache.
   * @param request_headers - headers from the request the CacheFilter is currently processing.
   * @return an enum indicating whether the request is eligible for serving from cache and/or having
   * its response stored in cache.
   */
  virtual RequestCacheability
  requestCacheability(const Http::RequestHeaderMap& request_headers) PURE;

  /**
   * Determines the cacheability of the response during encoding.
   * @param request_headers - headers from the request the CacheFilter is currently processing.
   * @param response_headers - headers from the upstream response the CacheFilter is currently
   * processing.
   * @return an enum indicating how the response should be handled.
   */
  virtual ResponseCacheability
  responseCacheability(const Http::RequestHeaderMap& request_headers,
                       const Http::ResponseHeaderMap& response_headers) PURE;

  /**
   * Determines whether the cached entry may be used directly or must be validated with upstream.
   * @param request_headers - request headers associated with the response_headers.
   * @param cached_response_headers - headers from the cached response.
   * @param content_length - the byte length of the cached content.
   * @param cached_metadata - the metadata that has been stored along side the cached entry.
   * @param now - the timestamp for this request.
   * @return details about whether or not the cached entry can be used.
   */
  virtual CacheEntryUsability
  cacheEntryUsability(const Http::RequestHeaderMap& request_headers,
                      const Http::ResponseHeaderMap& cached_response_headers,
                      const uint64_t content_length, const ResponseMetadata& cached_metadata,
                      SystemTime now) PURE;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
