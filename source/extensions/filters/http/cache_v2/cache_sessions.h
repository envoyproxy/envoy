#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"

#include "source/extensions/filters/http/cache_v2/http_cache.h"
#include "source/extensions/filters/http/cache_v2/key.pb.h"
#include "source/extensions/filters/http/cache_v2/stats.h"
#include "source/extensions/filters/http/cache_v2/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

class ActiveLookupRequest {
public:
  // Prereq: request_headers's Path(), Scheme(), and Host() are non-null.
  ActiveLookupRequest(
      const Http::RequestHeaderMap& request_headers,
      UpstreamRequestFactoryPtr upstream_request_factory, absl::string_view cluster_name,
      Event::Dispatcher& dispatcher, SystemTime timestamp,
      const std::shared_ptr<const CacheableResponseChecker> cacheable_response_checker_,
      const std::shared_ptr<const CacheFilterStatsProvider> stats_provider_,
      bool ignore_request_cache_control_header);

  // Caches may modify the key according to local needs, though care must be
  // taken to ensure that meaningfully distinct responses have distinct keys.
  const Key& key() const { return key_; }

  Http::RequestHeaderMap& requestHeaders() const { return *request_headers_; }
  bool isCacheableResponse(const Http::ResponseHeaderMap& headers) const {
    return cacheable_response_checker_->isCacheableResponse(headers);
  }
  const std::shared_ptr<const CacheableResponseChecker>& cacheableResponseChecker() const {
    return cacheable_response_checker_;
  }
  const std::shared_ptr<const CacheFilterStatsProvider>& statsProvider() const {
    return stats_provider_;
  }
  CacheFilterStats& stats() const { return statsProvider()->stats(); }
  UpstreamRequestPtr createUpstreamRequest() const {
    return upstream_request_factory_->create(statsProvider());
  }
  Event::Dispatcher& dispatcher() const { return dispatcher_; }
  SystemTime timestamp() const { return timestamp_; }
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
  const std::shared_ptr<const CacheFilterStatsProvider> stats_provider_;
  // Time when this LookupRequest was created (in response to an HTTP request).
  SystemTime timestamp_;
  RequestCacheControl request_cache_control_;
};
using ActiveLookupRequestPtr = std::unique_ptr<ActiveLookupRequest>;

struct ActiveLookupResult {
  // The source from which headers, body and trailers can be retrieved. May be
  // a cache-reader CacheSession, or may be an UpstreamRequest if the request
  // was uncacheable. The filter doesn't need to know which.
  std::unique_ptr<HttpSource> http_source_;

  CacheEntryStatus status_;
};

using ActiveLookupResultPtr = std::unique_ptr<ActiveLookupResult>;
using ActiveLookupResultCallback = absl::AnyInvocable<void(ActiveLookupResultPtr)>;

// CacheSessions is a wrapper around an HttpCache which provides a shorter-lived in-memory
// cache of headers and already open cache entries. All the http-specific aspects of the
// cache (range requests, validation, etc.) are performed by the CacheSession
// so the HttpCache only needs to support simple read/write operations.
//
// May or may not be a singleton, depending on the specific cache extension; must include
// the Singleton::Instance interface to support cases when it is.
class CacheSessions : public Singleton::Instance, public CacheFilterStatsProvider {
public:
  // This is implemented in CacheSessionsImpl so that tests which only use a mock don't
  // need to build the real thing, but declared here so that the actual use-site can
  // create an instance without including the larger header.
  static std::shared_ptr<CacheSessions> create(Server::Configuration::FactoryContext& context,
                                               std::unique_ptr<HttpCache> cache);

  virtual void lookup(ActiveLookupRequestPtr request, ActiveLookupResultCallback&& cb) PURE;
  virtual HttpCache& cache() const PURE;
  CacheInfo cacheInfo() const { return cache().cacheInfo(); }
  ~CacheSessions() override = default;
};

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
