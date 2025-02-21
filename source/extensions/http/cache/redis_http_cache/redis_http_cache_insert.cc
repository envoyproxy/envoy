#include "source/extensions/http/cache/redis_http_cache/redis_http_cache.h"
#include "source/extensions/http/cache/redis_http_cache/redis_http_cache_insert.h"

//#include "source/extensions/filters/http/cache/cache_custom_headers.h"
//#include "source/extensions/http/cache/redis_http_cache/cache_header_proto_util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

void RedisHttpCacheInsertContext::insertHeaders(const Http::ResponseHeaderMap& response_headers,
                     const ResponseMetadata& metadata, InsertCallback cb,
                     bool end_stream) {
    insert_callback_ = std::move(cb);
  
  // Make proto with received headers. It will be sent to Redis as the last item (after body and trailers).
  header_proto_ = makeCacheFileHeaderProto(lookup_->key(),
                                         response_headers,
                                         metadata);

  std::weak_ptr<bool> weak = alive_;
  if(!tls_slot_->send(lookup_->clusterName(), {"set", fmt::format(RedisCacheHeadersEntry, stableHashKey(lookup_->key())), "", "NX", "EX", "30"}, 
  [this, weak, end_stream] (bool connected, bool success, absl::optional<std::string> /*redis_value*/) {
    // Session was destructed during the call to Redis.
    // Do nothing. Do not call callback because its context is gone.
    if (weak.expired()) {
        return;
    }

    if (!connected) {
        // The client could not successfully connect to the database.
        // Return false to indicate not to continue with caching.
        (insert_callback_)(false);
    }

    if (!success) {
        // Error writing to Redis. This may happen in the following situations:
        // - Entry containing headers exists. The entry was added most likely by other Envoy or by other thread.
        //   In both cases, do not attempt to update the cache.
        // - Error happened while writing to the database.
        (insert_callback_)(false);
    }
    
    if (end_stream) {
        onStreamEnd();
    }
    /*std::move*/(insert_callback_)(true);
    }))

 {
        // Callback must be executed on filter's thread.
        lookup_->dispatcher()->post([this](){ 
        (insert_callback_)(false);
        });
    }
    
}

void RedisHttpCacheInsertContext::insertBody(const Buffer::Instance& chunk, 
                InsertCallback ready_for_next_chunk,
                  bool end_stream) {
    
    insert_callback_ = std::move(ready_for_next_chunk);

  std::weak_ptr<bool> weak = alive_;
  Extensions::Common::Redis::RedisAsyncClient::ResultCallback result_callback =
    [this, weak, end_stream] (bool connected, bool success, absl::optional<std::string> /*redis_value*/) {
    // Session was destructed during the call to Redis.
    // Do nothing. Do not call callback because its context is gone.
    if (weak.expired()) {
        return;
    }

    if(!connected) {
       (insert_callback_)(false);
    }

    if (!success) {
       (insert_callback_)(false);
    }

    if (end_stream) {
        onStreamEnd();
    }
    /*std::move*/(insert_callback_)(true);
    };

    bool success = false;
    if (first_body_chunk_) {
    success =tls_slot_->send(lookup_->clusterName(), {"set", fmt::format(RedisCacheBodyEntry, stableHashKey(lookup_->key())), chunk.toString(), "EX", "30"}, 
                    std::move(result_callback));
    } else {
    success = tls_slot_->send(lookup_->clusterName(), {"append", fmt::format(RedisCacheBodyEntry, stableHashKey(lookup_->key())), chunk.toString()}, 
                    std::move(result_callback));
    }

    if(!success) {
        // Callback must be executed on filter's thread.
        lookup_->dispatcher()->post([this](){ 
        (insert_callback_)(false);
        });
        return;
    }

    body_length_ += chunk.length();
    first_body_chunk_ = false;
}

  void RedisHttpCacheInsertContext::insertTrailers(const Http::ResponseTrailerMap& trailers,
                      InsertCallback insert_complete) {
    insert_callback_ = std::move(insert_complete);

    CacheFileTrailer trailers_proto = makeCacheFileTrailerProto(trailers);

  std::weak_ptr<bool> weak = alive_;
  if(!tls_slot_->send(lookup_->clusterName(), {"set", fmt::format(RedisCacheTrailersEntry, stableHashKey(lookup_->key())), trailers_proto.SerializeAsString(), "EX", "30"},
    [this, weak ] (bool connected, bool success, absl::optional<std::string> /*redis_value*/) mutable {
    // Session was destructed during the call to Redis.
    // Do nothing. Do not call callback because its context is gone.
    if (weak.expired()) {
        return;
    }

    if(!connected) {
        (insert_callback_)(false);
        return;
    }

    // This is the end of the stream.
    if(success) {
        header_proto_.set_trailers(true);
        onStreamEnd();
    }
    /*std::move*/(insert_callback_)(success);
    }))
    {
        // Callback must be executed on filter's thread.
        lookup_->dispatcher()->post([this](){ 
        (insert_callback_)(false);
        });
    }
  }


// This is called when the last byte of data which needs to be cached
// has been received. At that moment the size of the body is known
// and whether trailers were present. That info is used to update the
// main block in redis.
void RedisHttpCacheInsertContext::onStreamEnd() {
  // TODO: for now cache for 1h. This can be changed based on values in headers.
  // When the cache expires, the new request will cache it again for 1h.
  std::string cache_for = "3000000";

    if(!tls_slot_->send(lookup_->clusterName(), {"expire", fmt::format(RedisCacheBodyEntry, stableHashKey(lookup_->key())), fmt::format("{}", cache_for)}, 
  [] (bool /* connected */, bool /*success*/, absl::optional<std::string> /*redis_value*/) {})) {
        return;
    }

    if(!tls_slot_->send(lookup_->clusterName(), {"expire", fmt::format(RedisCacheTrailersEntry, stableHashKey(lookup_->key())), fmt::format("{}", cache_for)}, 
  [] (bool /* connected */, bool /*success*/, absl::optional<std::string> /*redis_value*/) {})) {
        return;
    }


  header_proto_.set_body_size(body_length_); 
  std::string serialized_proto = header_proto_.SerializeAsString();

    tls_slot_->send(lookup_->clusterName(), {"set", fmt::format(RedisCacheHeadersEntry, stableHashKey(lookup_->key())), serialized_proto, "XX", "EX", fmt::format("{}", cache_for)},
  [] (bool /* connected */, bool /*success*/, absl::optional<std::string> /*redis_value*/) {});
}

} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
