#pragma once

#include "envoy/http/header_map.h"
#include "envoy/stream_info/filter_state.h"

#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

struct CacheEntryUsability {
  CacheEntryStatus status = CacheEntryStatus::Unusable;
  Envoy::Seconds age = Envoy::Seconds::max();
};

class CachePolicyCallbacks {
 public:
  virtual ~CachePolicyCallbacks() = default;

  virtual const Envoy::StreamInfo::FilterStateSharedPtr& filterState() PURE;
};

// CachePolicy is an extension point for deployment specific caching behavior.
class CachePolicy {
public:
  virtual ~CachePolicy() = default;

  // createCacheKey calculates the lookup key for storing the entry in the cache.
  virtual Key createCacheKey(const Envoy::Http::RequestHeaderMap& request_headers) PURE;

  // requestCacheable modifies the cacheability of the response during
  // decoding. request_cache_control is the result of parsing the request's
  // Cache-Control header, parsed by the caller.
  virtual bool requestCacheable(const Envoy::Http::RequestHeaderMap& request_headers,
                                const RequestCacheControl& request_cache_control) PURE;

  // responseCacheable modifies the cacheability of the response during
  // encoding. response_cache_control is the result of parsing the response's
  // Cache-Control header, parsed by the caller.
  virtual bool responseCacheable(const Envoy::Http::RequestHeaderMap& request_headers,
                                 const Envoy::Http::ResponseHeaderMap& response_headers,
                                 const ResponseCacheControl& response_cache_control,
                                 const VaryHeader& vary_allow_list) PURE;

  // computeCacheEntryUsability calculates whether the cached entry may be used
  // directly or must be validated with upstream. request_cache_control and
  // response_cache_control are the result of parsing the request's and
  // response's Cache-Control header, respectively, parsed by the caller.
  virtual CacheEntryUsability
  computeCacheEntryUsability(const Envoy::Http::RequestHeaderMap& request_headers,
                             const Envoy::Http::ResponseHeaderMap& cached_response_headers,
                             const RequestCacheControl& request_cache_control,
                             const ResponseCacheControl& cached_response_cache_control,
                             const uint64_t content_length, const ResponseMetadata& cached_metadata,
                             Envoy::SystemTime now) PURE;

  // setCallbacks allows additional callbacks to be set when the CacheFilter
  // sets decoder filter callbacks.
  virtual void setCallbacks(CachePolicyCallbacks& callbacks) PURE;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
