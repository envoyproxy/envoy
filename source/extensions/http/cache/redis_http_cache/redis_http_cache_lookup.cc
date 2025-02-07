#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/http/cache/redis_http_cache/redis_http_cache_lookup.h"
#include "source/extensions/http/cache/redis_http_cache/cache_header_proto_util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

void RedisHttpCacheLookupContext::getHeaders(LookupHeadersCallback&& cb) {
    // TODO: can I capture the cb instead of storing it in this.cb_?
    cb_ = std::move(cb);

  // TODO: handle here situation when client cannot connect to the redis server.
    // maybe connect it when the first request comes and it is not connected.

    // Get trailers proto and add headers.
    // Convert to string and add to redis command.

  tls_slot_->send(fmt::format(RedisGetHeadersCmd, stableHashKey(lookup_.key())),
    [this ] (bool connected, bool success, absl::optional<std::string> redis_value) mutable {

    if (!connected) {
        // Failure to connect to Redis. Proceed without additional attempts
        // to connect.
        LookupResult lookup_result;
        lookup_result.cache_entry_status_ = CacheEntryStatus::LookupError;
        (cb_)(std::move(lookup_result), /* end_stream (ignored) = */ false);
        return;
    }
    if (!success) {
        std::cout << "Nothing found in the database.\n";
        // TODO: end_stream should be taken based on info from cache.

        (cb_)(LookupResult{}, /* end_stream (ignored) = */ false);
        return;
    }

    // We need to strip quotes on both sides of the string.
    // TODO: maybe move to redis async client.
    if (redis_value.value().length() == 4)  {
        redis_value.value().clear();
    } else {
        redis_value = redis_value.value().substr(1, redis_value.value().length() - 2);
    }


    // Entry is in redis, but is empty. It means that some other entity
    // is filling the cache.
    if (redis_value.value().length() == 0) {
        // Entry exists but is empty. It means that some other entity is filling the cache.
        // Continue as if the cache entry was not found.
        LookupResult lookup_result;
        lookup_result.cache_entry_status_ = CacheEntryStatus::LookupError;
        (cb_)(std::move(lookup_result), /* end_stream (ignored) = */ false);
        return;
    }

  CacheFileHeader header;
  header.ParseFromString(redis_value.value());

    if (header.headers().size() == 0) {
        std::cout << "Nothing found in the database.\n";

        (cb_)(LookupResult{}, /* end_stream (ignored) = */ false);
        return;
    }

    // get headers from proto.
    Http::ResponseHeaderMapPtr headers = headersFromHeaderProto(header);

    auto body_size = header.body_size();
    has_trailers_ = header.trailers();
    // This is stream end when there is no body and there are no trailers in the cache.
    bool stream_end = (body_size == 0) && (!has_trailers_);
    /*std::move*/(cb_)(lookup_.makeLookupResult(std::move(headers), metadataFromHeaderProto(header), body_size), stream_end);
    }
);
}

void RedisHttpCacheLookupContext::getTrailers(LookupTrailersCallback&& cb) {
    cb2_ = std::move(cb);

  tls_slot_->send(fmt::format(RedisGetTrailersCmd, stableHashKey(lookup_.key())),
    [this ] (bool connected, bool success, absl::optional<std::string> redis_value) mutable {
    if(!connected) {
        ASSERT(false);
    }
    

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
    redis_value = redis_value.value().substr(1, redis_value.value().length() - 2);

    CacheFileTrailer trailers;
    trailers.ParseFromString(redis_value.value());
    cb2_(trailersFromTrailerProto(trailers));
    }
);
} 
    
} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
