#pragma once
#include "source/extensions/filters/http/cache/http_cache.h"
#include "source/extensions/http/cache/redis_http_cache/redis_http_cache_client.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

class RedisHttpCacheLookupContext : public LookupContext {
public:
  RedisHttpCacheLookupContext(absl::string_view cluster_name, Event::Dispatcher& dispatcher,
                              ThreadLocal::TypedSlot<ThreadLocalRedisClient>& tls_slot,
                              LookupRequest&& lookup)
      : dispatcher_(dispatcher), key_(lookup.key()), tls_slot_(tls_slot),
        lookup_(std::move(lookup)), cluster_name_(cluster_name) {
    alive_ = std::make_shared<bool>(true);
  }

  // From LookupContext
  void getHeaders(LookupHeadersCallback&&) final;
  void getBody(const AdjustedByteRange&, LookupBodyCallback&&) final;
  void getTrailers(LookupTrailersCallback&&) final;
  void onDestroy() final {}
  ~RedisHttpCacheLookupContext() override {}

  const LookupRequest& lookup() const { return lookup_; }
  const Key& key() const { return key_; }
  Event::Dispatcher* dispatcher() const { return &dispatcher_; }
  absl::string_view clusterName() const { return cluster_name_; }

private:
  Event::Dispatcher& dispatcher_;
  Key key_;

  // This is used to derive weak pointer given to callbacks.
  // Callbacks check if the weak pointer is still valid.
  // If the weak pointer is expired, it means that the session which issued the
  // call to redis has been closed and the associated cache filter has been
  // destroyed.
  std::shared_ptr<bool> alive_;

  LookupHeadersCallback lookup_headers_callback_;
  LookupBodyCallback lookup_body_callback_;
  LookupTrailersCallback lookup_trailers_callback_;

  ThreadLocal::TypedSlot<ThreadLocalRedisClient>& tls_slot_;
  const LookupRequest lookup_;
  bool has_trailers_;
  absl::string_view cluster_name_;
};

} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
