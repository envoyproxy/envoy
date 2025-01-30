#include "source/extensions/http/cache/redis_http_cache/redis_http_cache.h"

#include "source/extensions/filters/http/cache/cache_custom_headers.h"
#include "source/extensions/http/cache/redis_http_cache/cache_header_proto_util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {



void RedisHttpCacheLookupContext::getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb)
{
    cb1_ = std::move(cb);
  // TODO: handle here situation when client cannot connect to the redis server.
    // maybe connect it when the first request comes and it is not connected.

    
  tls_slot_->send(fmt::format("getrange cache-{}-body {} {}", stableHashKey(lookup_.key()), range.begin(), range.begin() + range.length() - 1),
    [this] (bool success, std::string redis_value) mutable {
    if (!success) {
        // TODO: make sure that this path is tested.
        ASSERT(false);
        std::cout << "Nothing found in the database.\n";
        // TODO: end_stream should be taken based on info from cache.

        //(cb_)(LookupResult{}, /* end_stream (ignored) = */ true); true -> will not call getBody.
        return;
    }

    // We need to strip quotes on both sides of the string.
    // TODO: maybe move to redis async client.
    redis_value = redis_value.substr(1, redis_value.length() - 2);

  // TODO: this is not very efficient.
  std::unique_ptr<Buffer::OwnedImpl> buf;
    buf = std::make_unique<Buffer::OwnedImpl>();
    buf->add(redis_value);
        /*std::move(cb1_)*/cb1_(std::move(buf), !has_trailers_);
    });
}






LookupContextPtr RedisHttpCache::makeLookupContext(LookupRequest&& lookup,
                                                        Http::StreamFilterCallbacks& callbacks) {
  return std::make_unique<RedisHttpCacheLookupContext>(callbacks.dispatcher(), /**this,*/ cluster_manager_, tls_slot_, std::move(lookup));
}

InsertContextPtr RedisHttpCache::makeInsertContext(LookupContextPtr&& lookup,
                                      Http::StreamFilterCallbacks&/* callbacks*/) {
  auto redis_lookup_context = std::unique_ptr<RedisHttpCacheLookupContext>(
      dynamic_cast<RedisHttpCacheLookupContext*>(lookup.release()));
  return std::make_unique<RedisHttpCacheInsertContext>(std::move(redis_lookup_context), cluster_manager_, tls_slot_);
    
    }

} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
