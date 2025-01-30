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
    // Allocate thread local slot, where async client and queue with pending requests are stored.
    auto* cluster = cluster_manager_.getThreadLocalCluster("redis_cluster");
     if (!cluster) {
        ASSERT(false);
    }

    cb_ = std::move(cb);
    // allocate thread local tcp async client and wrap it with redis protocol 
    tls_slot_->redis_client_.client_ = cluster->tcpAsyncClient(nullptr, std::make_shared<const Tcp::AsyncTcpClientOptions>(false));
    tls_slot_->redis_client_.client_->setAsyncTcpClientCallbacks(tls_slot_->redis_client_);
    tls_slot_->redis_client_.callback_ = [this, end_stream] (bool success, std::string /*redis_value*/) {

    if (!success) {
        // Error writing to Redis. This may happen in the following situations:
        // - Entry containing headers exists. The entry was added most likely by other Envoy or by other thread.
        //   In both cases, do not attempt to update the cache.
        // - Error happened while writing to the database.
        
        ASSERT(false); 
    }
    
    //ResponseMetadata metadata;

    ASSERT(cb_);
    //ASSERT(lookup_);

 
    // TODO: check what value true means here. What if there is an error talking to redis?
    // Should it be false?
    if (end_stream) {
        onStreamEnd();
    }
    /*std::move*/(cb_)(true);


     
    };
  // TODO: handle here situation when client cannot connect to the redis server.
    // maybe connect it when the first request comes and it is not connected.
    tls_slot_->redis_client_.client_->connect();
  
  //CacheFileHeader header_proto = makeCacheFileHeaderProto(lookup_->key(),
  header_proto_ = makeCacheFileHeaderProto(lookup_->key(),
                                         response_headers,
                                         metadata);
#if 0
  g_header_proto = makeCacheFileHeaderProto(lookup_->key(),
                                         response_headers,
                                         metadata);
    ASSERT(g_header_proto.headers().size() != 0);
#endif
  Buffer::OwnedImpl buf;
NetworkFilters::Common::Redis::RespValue request;
  std::vector<NetworkFilters::Common::Redis::RespValue> values(6);

#if 0
  // Original query.
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[0].asString() = "hmset";
  values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[1].asString() = absl::StrCat("cache-", stableHashKey(lookup_->key()));
  values[2].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[2].asString() = "headers";
  values[3].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[3].asString() = header_proto.SerializeAsString();
#endif

  // Set a key cache-<hash> with options:
  // NX - if such key does not exist
  // EX - expire within 30 seconds (gettting response from upstream must complete within 30 secs
  //      otherwise, it will be deleted not to leave unfinished cache entry in half-finished state.
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[0].asString() = "set";
  values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[1].asString() = absl::StrCat("cache-", stableHashKey(lookup_->key()));
  values[2].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[2].asString() = "\"\"";
  values[3].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[3].asString() = "NX";
  values[4].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[4].asString() = "EX";
  values[5].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[5].asString() = "30";
  //values[3].type(NetworkFilters::Common::Redis::RespType::BulkString);
  //values[3].asString() = header_proto.SerializeAsString();

  request.type(NetworkFilters::Common::Redis::RespType::Array);
  request.asArray().swap(values);
  tls_slot_->redis_client_.encoder_.encode(request, buf);  

  tls_slot_->redis_client_.client_->write(buf, false);

//auto client = cluster->tcpAsyncClient(nullptr, std::make_shared<const Tcp::AsyncTcpClientOptions>(false));
    
}

void RedisHttpCacheInsertContext::insertBody(const Buffer::Instance& chunk, 
                InsertCallback ready_for_next_chunk,
                  bool end_stream) {
    
    cb_ = std::move(ready_for_next_chunk);
    // TODO: the client should be already connectedt to redis. We should check it here. 
    tls_slot_->redis_client_.callback_ = [this, end_stream] (bool success, std::string /*redis_value*/) {
    //ASSERT(false);

    if (!success) {
        // How to simulate this situation? When redis client reports that writing was not successful?
        // What if there is network error, reset. Is it reported here?
        ASSERT(false);
    }
    // If things are OK, we should call ready_for_next_chunk.
    //ready_for_next_chunk(true);
    //ResponseMetadata metadata;
    //ready_for_next_chunk(true);
    //return;

    //ASSERT(cb_);
    //ASSERT(lookup_);

 
    // TODO: check what value true means here. What if there is an error talking to redis?
    // Should it be false?
    if (end_stream) {
        onStreamEnd();
    }
    /*std::move*/(cb_)(true);
    };


    body_length_ += chunk.length();
  Buffer::OwnedImpl buf;
NetworkFilters::Common::Redis::RespValue request;
  if (first_body_chunk_) {
    first_body_chunk_ = false;
    std::vector<NetworkFilters::Common::Redis::RespValue> values(5);
    values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
     values[0].asString() = "set";
    values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
    values[1].asString() = absl::StrCat("cache-", stableHashKey(lookup_->key()), "-body");
    values[2].type(NetworkFilters::Common::Redis::RespType::BulkString);
    values[2].asString() = chunk.toString();;
    values[3].type(NetworkFilters::Common::Redis::RespType::BulkString);
    values[3].asString() = "EX";
    values[4].type(NetworkFilters::Common::Redis::RespType::BulkString);
    values[4].asString() = "30";

    request.type(NetworkFilters::Common::Redis::RespType::Array);
    request.asArray().swap(values);
    } else {
    std::vector<NetworkFilters::Common::Redis::RespValue> values(3);
    values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
     values[0].asString() = "append";
    values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
    values[1].asString() = absl::StrCat("cache-", stableHashKey(lookup_->key()), "-body");
    values[2].type(NetworkFilters::Common::Redis::RespType::BulkString);
    values[2].asString() = chunk.toString();;

    request.type(NetworkFilters::Common::Redis::RespType::Array);
    request.asArray().swap(values);
  }
  tls_slot_->redis_client_.encoder_.encode(request, buf);  

  tls_slot_->redis_client_.client_->write(buf, false);
}

// This is called when the last byte of data which needs to be cached
// has been received. At this moment the size of the body is known
// and whether trailers were present. That info is used to update the
// main block in redis.
void RedisHttpCacheInsertContext::onStreamEnd() {
    // This is called when all data has been received and main entry must be updated
    // with headers, body size and trailers.
  // Now we know the total size of the body and whether trailers were present. Update the main
  // headers block.
  std::string cache_for = "3000000";
NetworkFilters::Common::Redis::RespValue request;
  Buffer::OwnedImpl buf;
  tls_slot_->redis_client_.callback_ = [/*this, cache_for*/] (bool /*success*/, std::string /*redis_value*/) {
  // If redis client can handle queueing, this can be invoked immediatelky after sending
  // expire for body and this callback can be {}
#if 0
NetworkFilters::Common::Redis::RespValue request;
  Buffer::OwnedImpl buf1;

    std::vector<NetworkFilters::Common::Redis::RespValue> values(6);
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[0].asString() = "set";
  values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[1].asString() = absl::StrCat("cache-", stableHashKey(lookup_->key()));
  values[2].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[2].asString() = header_proto_.SerializeAsString();
    values[3].type(NetworkFilters::Common::Redis::RespType::BulkString);
    values[3].asString() = "XX";
    values[4].type(NetworkFilters::Common::Redis::RespType::BulkString);
    values[4].asString() = "EX";
    values[5].type(NetworkFilters::Common::Redis::RespType::BulkString);
    values[5].asString() = cache_for; 

    request.type(NetworkFilters::Common::Redis::RespType::Array);
    request.asArray().swap(values);
  tls_slot_->redis_client_.encoder_.encode(request, buf1);  

  tls_slot_->redis_client_.client_->write(buf1, false);
#endif
};

  header_proto_.set_body_size(body_length_); 

    std::vector<NetworkFilters::Common::Redis::RespValue> body_values(3);
    body_values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
     body_values[0].asString() = "expire";
    body_values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
    body_values[1].asString() = absl::StrCat("cache-", stableHashKey(lookup_->key()), "-body");
    body_values[2].type(NetworkFilters::Common::Redis::RespType::BulkString);
    body_values[2].asString() = cache_for;

    request.type(NetworkFilters::Common::Redis::RespType::Array);
    request.asArray().swap(body_values);
  tls_slot_->redis_client_.encoder_.encode(request, buf);  

  tls_slot_->redis_client_.client_->write(buf, false);

  // First update expiry for body and trailers. After that update
  // the main headers block. If any operation fails, redis will clear itself.

  Buffer::OwnedImpl buf1;

    std::vector<NetworkFilters::Common::Redis::RespValue> values(6);
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[0].asString() = "set";
  values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[1].asString() = absl::StrCat("cache-", stableHashKey(lookup_->key()));
  values[2].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[2].asString() = header_proto_.SerializeAsString();
    values[3].type(NetworkFilters::Common::Redis::RespType::BulkString);
    values[3].asString() = "XX";
    values[4].type(NetworkFilters::Common::Redis::RespType::BulkString);
    values[4].asString() = "EX";
    values[5].type(NetworkFilters::Common::Redis::RespType::BulkString);
    values[5].asString() = cache_for; 

    request.type(NetworkFilters::Common::Redis::RespType::Array);
    request.asArray().swap(values);
  tls_slot_->redis_client_.encoder_.encode(request, buf1);  

  tls_slot_->redis_client_.client_->write(buf1, false);

}


  void RedisHttpCacheInsertContext::insertTrailers(const Http::ResponseTrailerMap& trailers,
                      InsertCallback insert_complete) {
    // TODO: can I capture the cb instead of storing it in this.cb_?
    cb1_ = std::move(insert_complete);

    CacheFileTrailer trailers_proto = makeCacheFileTrailerProto(trailers);
  // TODO: handle here situation when client cannot connect to the redis server.
    // maybe connect it when the first request comes and it is not connected.

  tls_slot_->send(fmt::format(RedisInsertTrailersCmd, stableHashKey(lookup_->key()), trailers_proto.SerializeAsString()),
    [this ] (bool success, std::string /*redis_value*/) mutable {

    // This is the end of the stream.
    if(success) {
        header_proto_.set_trailers(true);
        onStreamEnd();
    }
    /*std::move*/(cb1_)(success);

    });
  }

} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
