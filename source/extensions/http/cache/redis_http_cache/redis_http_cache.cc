#include "source/extensions/http/cache/redis_http_cache/redis_http_cache.h"

#include "source/extensions/filters/http/cache/cache_custom_headers.h"
#include "source/extensions/http/cache/redis_http_cache/cache_header_proto_util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {









LookupContextPtr RedisHttpCache::makeLookupContext(LookupRequest&& lookup,
                                                        Http::StreamFilterCallbacks& callbacks) {
  return std::make_unique<RedisHttpCacheLookupContext>(callbacks.dispatcher(), /**this,*/ cluster_manager_, tls_slot_, std::move(lookup));
}

InsertContextPtr RedisHttpCache::makeInsertContext(LookupContextPtr&& lookup,
                                      Http::StreamFilterCallbacks&/* callbacks*/) {
  auto redis_lookup_context = std::unique_ptr<RedisHttpCacheLookupContext>(
      dynamic_cast<RedisHttpCacheLookupContext*>(lookup.release()));
  return std::make_unique<RedisHttpCacheInsertContext>(std::move(redis_lookup_context), tls_slot_);
    
    }

} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
