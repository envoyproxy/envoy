#pragma once

#include "envoy/extensions/filters/http/cache/v3/cache.pb.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/cache/http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class CacheFilter;

class ThunderingHerdRetryInterface {
public:
  virtual void retryHeaders(Http::RequestHeaderMap& request_headers) PURE;
  virtual ~ThunderingHerdRetryInterface() = default;
};

class ThunderingHerdHandler {
public:
  static std::shared_ptr<ThunderingHerdHandler> create(
      const envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler& config,
      Server::Configuration::CommonFactoryContext& context);

  /**
   * Either continues encoding, or blocks waiting for a previous request to complete
   * before retrying the cache lookup.
   * @param weak_filter the filter to be blocked or continued.
   * @param decoder_callbacks the decoder_callbacks from the filter.
   * @param key the key of the cache entry.
   * @param request_headers the request headers from the filter.
   */
  virtual void handleUpstreamRequest(std::weak_ptr<ThunderingHerdRetryInterface> weak_filter,
                                     Http::StreamDecoderFilterCallbacks* decoder_callbacks,
                                     const Key& key, Http::RequestHeaderMap& request_headers) PURE;

  enum class InsertResult { Inserted, Failed, NotCacheable };

  /**
   * @param key is the cache key for the request
   * @param insert_succeeded_or_unneeded is true if a cache insert completed successfully,
   *        or if the entry is not cacheable
   */
  virtual void handleInsertFinished(const Key& key, InsertResult insert_result) PURE;

  virtual ~ThunderingHerdHandler() = default;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
