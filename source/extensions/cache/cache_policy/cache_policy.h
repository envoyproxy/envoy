#pragma once

#include "envoy/extensions/cache/cache_policy/v3/config.pb.h"
#include "envoy/extensions/cache/cache_policy/v3/config.pb.validate.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/extensions/filters/http/cache/cache_entry_utils.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/key.pb.h"

namespace Envoy {
namespace Extensions {
namespace Cache {

/**
 * Contains information about whether the cache entry is usable.
 */
struct CacheEntryUsability {
  /**
   * Whether the cache entry is usable, additional checks are required to be usable, or unusable.
   */

  HttpFilters::Cache::CacheEntryStatus status = HttpFilters::Cache::CacheEntryStatus::Unusable;
  /**
   * Value to be put in the Age header for cache responses.
   */
  Seconds age = Seconds::max();

  friend bool operator==(const CacheEntryUsability& a, const CacheEntryUsability& b) {
    return std::tie(a.status, a.age) == std::tie(b.status, b.age);
  }

  friend bool operator!=(const CacheEntryUsability& a, const CacheEntryUsability& b) {
    return !(a == b);
  }
};

class CachePolicyCallbacks {
public:
  virtual ~CachePolicyCallbacks() = default;

  virtual const StreamInfo::FilterStateSharedPtr& filterState() PURE;
};
using CachePolicyCallbacksPtr = std::unique_ptr<CachePolicyCallbacks>;

/**
 * An extension point for deployment specific caching behavior.
 */
class CachePolicy {
public:
  virtual ~CachePolicy() = default;

  /**
   * Calculates the lookup key for storing the entry in the cache.
   * @param request_headers - headers from the request the CacheFilter is currently processing.
   */
  virtual HttpFilters::Cache::Key createCacheKey(const Http::RequestHeaderMap& request_headers) PURE;

  /**
   * Determines the cacheability of the response during decoding.
   * @param request_headers - headers from the request the CacheFilter is currently processing.
   * @param request_cache_control - the result of parsing the request's Cache-Control header, parsed
   * by the caller.
   * @return true if the response may be cached, based on the contents of the request.
   */
  virtual bool requestCacheable(const Http::RequestHeaderMap& request_headers,
                                const HttpFilters::Cache::RequestCacheControl& request_cache_control) PURE;

  /**
   * Determines the cacheability of the response during encoding.
   * @param request_headers - headers from the request the CacheFilter is currently processing.
   * @param response_headers - headers from the upstream response the CacheFilter is currently
   * processing.
   * @param response_cache_control - the result of parsing the response's Cache-Control header,
   * parsed by the caller.
   * @param vary_allow_list - list of headers that the cache will respect when creating the Key for
   * Vary-differentiated responses.
   * @return true if the response may be cached.
   */
  virtual bool responseCacheable(const Http::RequestHeaderMap& request_headers,
                                 const Http::ResponseHeaderMap& response_headers,
                                 const HttpFilters::Cache::ResponseCacheControl& response_cache_control,
                                 const HttpFilters::Cache::VaryAllowList& vary_allow_list) PURE;

  /**
   * Determines whether the cached entry may be used directly or must be validated with upstream.
   * @param request_headers - request headers associated with the response_headers.
   * @param cached_response_headers - headers from the cached response.
   * @param request_cache_control - the parsed result of the request's Cache-Control header, parsed
   * by the caller.
   * @param cached_response_cache_control - the parsed result of the response's Cache-Control
   * header, parsed by the caller.
   * @param content_length - the byte length of the cached content.
   * @param cached_metadata - the metadata that has been stored along side the cached entry.
   * @param now - the timestamp for this request.
   * @return details about whether or not the cached entry can be used.
   */
  virtual CacheEntryUsability
  computeCacheEntryUsability(const Http::RequestHeaderMap& request_headers,
                             const Http::ResponseHeaderMap& cached_response_headers,
                             const HttpFilters::Cache::RequestCacheControl& request_cache_control,
                             const HttpFilters::Cache::ResponseCacheControl& cached_response_cache_control,
                             const uint64_t content_length, const HttpFilters::Cache::ResponseMetadata& cached_metadata,
                             SystemTime now) PURE;

  /**
   * Performs actions when StreamInfo and FilterState become available, for
   * example for logging and observability, or to adapt CacheFilter behavior based on
   * route-specific CacheFilter config.
   * @param callbacks - Gives access to StreamInfo and FilterState
   */
  virtual void setCallbacks(CachePolicyCallbacks& callbacks) PURE;
};
using CachePolicyPtr = std::unique_ptr<CachePolicy>;

class CachePolicyImpl : public CachePolicy {
public:
  HttpFilters::Cache::Key createCacheKey(const Http::RequestHeaderMap& request_headers) override;
  bool requestCacheable(const Http::RequestHeaderMap& request_headers,
                        const HttpFilters::Cache::RequestCacheControl& request_cache_control) override;

  bool responseCacheable(const Http::RequestHeaderMap& request_headers,
                         const Http::ResponseHeaderMap& response_headers,
                         const HttpFilters::Cache::ResponseCacheControl& response_cache_control,
                         const HttpFilters::Cache::VaryAllowList& vary_allow_list) override;

  CacheEntryUsability
  computeCacheEntryUsability(const Http::RequestHeaderMap& request_headers,
                             const Http::ResponseHeaderMap& cached_response_headers,
                             const HttpFilters::Cache::RequestCacheControl& request_cache_control,
                             const HttpFilters::Cache::ResponseCacheControl& cached_response_cache_control,
                             const uint64_t content_length, const HttpFilters::Cache::ResponseMetadata& cached_metadata,
                             SystemTime now) override;

  void setCallbacks([[maybe_unused]] CachePolicyCallbacks& callbacks) override {}

private:
  bool requiresValidation(const HttpFilters::Cache::RequestCacheControl& request_cache_control,
                          const HttpFilters::Cache::ResponseCacheControl& cached_response_cache_control,
                          const Http::ResponseHeaderMap& response_headers,
                          Seconds response_age) const;

  const absl::flat_hash_set<absl::string_view>& cacheableStatusCodes() const;
  const std::vector<const Http::LowerCaseString*>& conditionalHeaders() const;
};

} // namespace Cache
} // namespace Extensions
} // namespace Envoy
