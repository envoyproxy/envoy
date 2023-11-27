#pragma once

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
   * Remaining freshness lifetime.
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
  // This request
  Bypass,
  // This request is eligible for serving from cache, but its response must not be stored.
  NoStore,
};

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
