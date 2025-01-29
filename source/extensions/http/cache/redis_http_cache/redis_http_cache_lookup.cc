#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/http/cache/redis_http_cache/redis_http_cache_lookup.h"
#include "source/extensions/http/cache/redis_http_cache/cache_header_proto_util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

void RedisHttpCacheLookupContext::getHeaders(LookupHeadersCallback&& cb) {
    cb_ = std::move(cb);

  // TODO: handle here situation when client cannot connect to the redis server.
    // maybe connect it when the first request comes and it is not connected.

  tls_slot_->send(fmt::format(RedisGetHeadersCmd, stableHashKey(lookup_.key())),
    [this ] (bool success, std::string redis_value) mutable {

    if (!success) {
        std::cout << "Nothing found in the database.\n";
        // TODO: end_stream should be taken based on info from cache.

        (cb_)(LookupResult{}, /* end_stream (ignored) = */ false);
        return;
    }

    // We need to strip quotes on both sides of the string.
    // TODO: maybe move to redis async client.
    if (redis_value.length() == 4)  {
        redis_value.clear();
    } else {
        redis_value = redis_value.substr(1, redis_value.length() - 2);
    }


    // Entry is in redis, but is empty. It means that some other entity
    // is filling the cache.
    if (redis_value.length() == 0) {
        // Entry exists but is empty. It means that some other entity is filling the cache.
        // Continue as if the cache entry was not found.
        LookupResult lookup_result;
        lookup_result.cache_entry_status_ = CacheEntryStatus::LookupError;
        (cb_)(std::move(lookup_result), /* end_stream (ignored) = */ false);
        return;
    }

  CacheFileHeader header;
  header.ParseFromString(redis_value);

    if (header.headers().size() == 0) {
        std::cout << "Nothing found in the database.\n";
        // TODO: end_stream should be taken based on info from cache.

        (cb_)(LookupResult{}, /* end_stream (ignored) = */ false);
        return;
    }

    // get headers from proto.
    Http::ResponseHeaderMapPtr headers = headersFromHeaderProto(header);

    // TODO: get end_stream from header block from redis. 
    // Do not use fixed length of the body.
    /*std::move*/(cb_)(lookup_.makeLookupResult(std::move(headers), metadataFromHeaderProto(header), 6), false);
    }
);
}
    
} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
