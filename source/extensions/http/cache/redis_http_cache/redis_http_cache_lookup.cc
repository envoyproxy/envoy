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

    // Try to get headers from Redis. Passed callback is called when response is received
    // or error happens.
  std::weak_ptr<bool> weak = alive_;
  tls_slot_->send({"get", fmt::format(RedisCacheHeadersEntry, stableHashKey(lookup_.key()))},
    [this, weak] (bool connected, bool success, absl::optional<std::string> redis_value) mutable {
    // Session was destructed during the call to Redis.
    // Do nothing. Do not call callback because its context is gone.
    if (weak.expired()) {
        return;
    }

    if (!connected) {
        // Failure to connect to Redis. Proceed without additional attempts
        // to connect.
        LookupResult lookup_result;
        lookup_result.cache_entry_status_ = CacheEntryStatus::LookupError;
        (cb_)(std::move(lookup_result), /* end_stream (ignored) = */ false);
        return;
    }
    if (!success) {
        // Entry was not found. 
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
    // is filling the cache. Return the same value as when not found.
    if (redis_value.value().length() == 0) {
        LookupResult lookup_result;
        lookup_result.cache_entry_status_ = CacheEntryStatus::LookupError;
        (cb_)(std::move(lookup_result), /* end_stream (ignored) = */ false);
        return;
    }

  CacheFileHeader header;
  header.ParseFromString(redis_value.value());

    // Entry found, but its content is not as expected.
    if (header.headers().size() == 0) {
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

void RedisHttpCacheLookupContext::getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb)
{
    cb1_ = std::move(cb);

  std::weak_ptr<bool> weak = alive_;
  tls_slot_->send({"getrange", fmt::format(RedisCacheBodyEntry, stableHashKey(lookup_.key())), fmt::format("{}", range.begin()), fmt::format("{}", range.begin() + range.length() - 1)},
    [this, weak] (bool connected, bool success, absl::optional<std::string> redis_value) mutable {
    // Session was destructed during the call to Redis.
    // Do nothing. Do not call callback because its context is gone.
    if (weak.expired()) {
        return;
    }

    if (!connected) {
        // Connection to the redis server failed.
        (cb1_)(nullptr, true);
        return;
    }

    if (!success) {
        // Entry was not found in Redis.
        (cb1_)(nullptr, true);
        return;
    }

    // We need to strip quotes on both sides of the string.
    // TODO: maybe move to redis async client.
    redis_value = redis_value.value().substr(1, redis_value.value().length() - 2);

  // TODO: this is not very efficient.
  std::unique_ptr<Buffer::OwnedImpl> buf;
    buf = std::make_unique<Buffer::OwnedImpl>();
    buf->add(redis_value.value());
        /*std::move(cb1_)*/cb1_(std::move(buf), !has_trailers_);
    });
}

void RedisHttpCacheLookupContext::getTrailers(LookupTrailersCallback&& cb) {
    cb2_ = std::move(cb);

  std::weak_ptr<bool> weak = alive_;
  tls_slot_->send({"get", fmt::format(RedisCacheTrailersEntry, stableHashKey(lookup_.key()))},
    [this, weak] (bool connected, bool success, absl::optional<std::string> redis_value) mutable {

    // Session was destructed during the call to Redis.
    // Do nothing. Do not call callback because its context is gone.
    if (weak.expired()) {
        return;
    }
    if(!connected) {
        LookupResult lookup_result;
        lookup_result.cache_entry_status_ = CacheEntryStatus::LookupError;
        (cb2_)(nullptr);
        return;
    }
    
    if (!success) {
        (cb2_)(nullptr);
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
