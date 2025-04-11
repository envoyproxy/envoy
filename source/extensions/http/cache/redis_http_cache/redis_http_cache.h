#pragma once

#include "envoy/extensions/http/cache/redis_http_cache/v3/redis_http_cache.pb.h"
#include "envoy/extensions/http/cache/redis_http_cache/v3/redis_http_cache.pb.validate.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/redis/async_redis_client_impl.h"
#include "source/extensions/filters/http/cache/http_cache.h"
#include "source/extensions/http/cache/redis_http_cache/redis_http_cache_insert.h"
#include "source/extensions/http/cache/redis_http_cache/redis_http_cache_lookup.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

using ConfigProto = envoy::extensions::http::cache::redis_http_cache::v3::RedisHttpCacheConfig;

class RedisHttpCache : public HttpCache {
public:
  RedisHttpCache(absl::string_view cluster_name, Upstream::ClusterManager& cluster_manager,
                 ThreadLocal::SlotAllocator& tls)
      : cluster_name_(cluster_name), cluster_manager_(cluster_manager), tls_slot_(tls) {

    tls_slot_.set([&](Event::Dispatcher&) {
      return std::make_shared<ThreadLocalRedisClient>(cluster_manager_);
    });
  }
  LookupContextPtr makeLookupContext(LookupRequest&& /*lookup*/,
                                     Http::StreamFilterCallbacks& /* callbacks*/) override;
  InsertContextPtr
  makeInsertContext(LookupContextPtr&& /*lookup_context*/,
                    Http::StreamFilterCallbacks& /*callbacks*/) override; // {return nullptr;}
  CacheInfo cacheInfo() const override {
    CacheInfo info;

    return info;
  }

  void updateHeaders(const LookupContext& /*lookup_context*/,
                     const Http::ResponseHeaderMap& /*response_headers*/,
                     const ResponseMetadata& /*metadata*/,
                     UpdateHeadersCallback /*on_complete*/) override{};

  static absl::string_view name() { return "redis cache"; }

private:
  std::string cluster_name_;
  Upstream::ClusterManager& cluster_manager_;

  ThreadLocal::TypedSlot<ThreadLocalRedisClient> tls_slot_;
};

} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
