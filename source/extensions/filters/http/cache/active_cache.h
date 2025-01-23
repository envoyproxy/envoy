#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"

#include "source/extensions/filters/http/cache/http_cache.h"
#include "source/extensions/filters/http/cache/key.pb.h"
#include "source/extensions/filters/http/cache/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class ActiveLookupRequest {
public:
  // Prereq: request_headers's Path(), Scheme(), and Host() are non-null.
  ActiveLookupRequest(
      const Http::RequestHeaderMap& request_headers,
      UpstreamRequestFactoryPtr upstream_request_factory, absl::string_view cluster_name,
      Event::Dispatcher& dispatcher, SystemTime timestamp,
      const std::shared_ptr<const CacheableResponseChecker> cacheable_response_checker,
      bool ignore_request_cache_control_header);

  const RequestCacheControl& requestCacheControl() const { return request_cache_control_; }

  // Caches may modify the key according to local needs, though care must be
  // taken to ensure that meaningfully distinct responses have distinct keys.
  const Key& key() const { return key_; }

  Http::RequestHeaderMap& requestHeaders() const { return *request_headers_; }
  bool isCacheableResponse(const Http::ResponseHeaderMap& headers) const {
    return cacheable_response_checker_->isCacheableResponse(headers);
  }
  std::shared_ptr<const CacheableResponseChecker> cacheableResponseChecker() const {
    return cacheable_response_checker_;
  }
  UpstreamRequestFactory& upstreamRequestFactory() const { return *upstream_request_factory_; }
  Event::Dispatcher& dispatcher() const { return dispatcher_; }
  SystemTime timestamp() const { return timestamp_; }
  // Returns a copy of request_headers_ with validation headers added.
  Http::RequestHeaderMapPtr
  requestHeadersWithValidation(const Http::ResponseHeaderMap& response_headers) const;
  bool requiresValidation(const Http::ResponseHeaderMap& response_headers,
                          SystemTime::duration age) const;
  absl::optional<std::vector<RawByteRange>> parseRange() const;
  bool isRangeRequest() const;

private:
  void initializeRequestCacheControl(const Http::RequestHeaderMap& request_headers);

  UpstreamRequestFactoryPtr upstream_request_factory_;
  Event::Dispatcher& dispatcher_;
  Key key_;
  std::vector<RawByteRange> request_range_spec_;
  Http::RequestHeaderMapPtr request_headers_;
  const std::shared_ptr<const CacheableResponseChecker> cacheable_response_checker_;
  // Time when this LookupRequest was created (in response to an HTTP request).
  SystemTime timestamp_;
  RequestCacheControl request_cache_control_;
};
using ActiveLookupRequestPtr = std::unique_ptr<ActiveLookupRequest>;

struct ActiveLookupResult {
  // The source from which headers, body and trailers can be retrieved. May be
  // a cache-reader ActiveCacheEntry, or may be an UpstreamRequest if the request
  // was uncacheable. The filter doesn't need to know which.
  std::unique_ptr<HttpSource> http_source_;

  CacheEntryStatus status_;
};

using ActiveLookupResultPtr = std::unique_ptr<ActiveLookupResult>;
using ActiveLookupResultCallback = absl::AnyInvocable<void(ActiveLookupResultPtr)>;

// May or may not be a singleton; must include the interface for the case when it is.
class ActiveCache : public Singleton::Instance {
public:
  // This is implemented in ActiveCacheImpl so that tests which only use a mock don't
  // need to build the real thing, but declared here so that the actual use-site can
  // create an instance without including the larger header.
  static std::shared_ptr<ActiveCache> create(TimeSource& time_source,
                                             std::unique_ptr<HttpCache> cache);

  virtual void lookup(ActiveLookupRequestPtr request, ActiveLookupResultCallback&& cb) PURE;
  virtual HttpCache& cache() const PURE;
  CacheInfo cacheInfo() const { return cache().cacheInfo(); }
  ~ActiveCache() override = default;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
