#include "source/extensions/http/cache/redis_http_cache/redis_http_cache.h"

#include "source/extensions/filters/http/cache/cache_custom_headers.h"
#include "source/extensions/http/cache/redis_http_cache/cache_header_proto_util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

class RedisHttpCacheLookupContext : public LookupContext {
public:
  RedisHttpCacheLookupContext(Event::Dispatcher& dispatcher, 
   RedisHttpCache& cache, 
   Extensions::Common::Redis::RedisAsyncClient&/* redis_client*/,
                    Upstream::ClusterManager& cluster_manager,
                                      ThreadLocal::TypedSlot<RedisHttpCache::ThreadLocalRedisClient>& tls_slot,
                    LookupRequest&& lookup)
      : dispatcher_(dispatcher), cache_(cache), key_(lookup.key()), tls_slot_(tls_slot), lookup_(std::move(lookup)),
        /*redis_client_(redis_client),*/ cluster_manager_(cluster_manager) {}

  // From LookupContext
  void getHeaders(LookupHeadersCallback&&/* cb*/) final;// {ASSERT(false);}
  void getBody(const AdjustedByteRange& /*range*/, LookupBodyCallback&&/* cb*/) final; // {ASSERT(false);}
  void getTrailers(LookupTrailersCallback&&/* cb*/) final {ASSERT(false);}
  void onDestroy() final {/*ASSERT(false);*/}
  // This shouldn't be necessary since onDestroy is supposed to always be called, but in some
  // tests it is not.
  ~RedisHttpCacheLookupContext() override {/* onDestroy();*/ }

  const LookupRequest& lookup() const { return lookup_; }
  const Key& key() const { return key_; }
  //bool workInProgress() const;
  Event::Dispatcher* dispatcher() const { return &dispatcher_; }

  LookupHeadersCallback cb_;
  LookupBodyCallback cb1_;

private:
  void doCacheMiss();
  void doCacheEntryInvalid();
  void getHeaderBlockFromFile();
  void getHeadersFromFile();
  void closeFileAndGetHeadersAgainWithNewVaryKey();

  // In the event that the cache failed to retrieve, remove the cache entry from the
  // cache so we don't keep repeating the same failure.
  void invalidateCacheEntry();

  Event::Dispatcher& dispatcher_;

  // Cache defines which cluster to use.
  RedisHttpCache& cache_;

  Key key_;

  LookupHeadersCallback lookup_headers_callback_;
                                      ThreadLocal::TypedSlot<RedisHttpCache::ThreadLocalRedisClient>& tls_slot_;
  const LookupRequest lookup_;
  //Extensions::Common::Redis::RedisAsyncClient& redis_client_;
  Upstream::ClusterManager& cluster_manager_; 
};

void RedisHttpCacheLookupContext::getHeaders(LookupHeadersCallback&& cb) {
    // Allocate thread local slot, where async client and queue with pending requests are stored.
    auto* cluster = cluster_manager_.getThreadLocalCluster("redis_cluster");
     if (!cluster) {
        ASSERT(false);
    }

    cb_ = std::move(cb);
    // allocate thread local tcp async client and wrap it with redis protocol 
    tls_slot_->redis_client_.client_ = cluster->tcpAsyncClient(nullptr, std::make_shared<const Tcp::AsyncTcpClientOptions>(false));
    tls_slot_->redis_client_.client_->setAsyncTcpClientCallbacks(tls_slot_->redis_client_);
    tls_slot_->redis_client_.callback_ = [this] (bool success, std::string redis_value) {

    if (!success) {
        std::cout << "Nothing found in the database.\n";
        // TODO: end_stream should be taken based on info from cache.

        //(cb_)(LookupResult{}, /* end_stream (ignored) = */ true); true -> will not call getBody.
        (cb_)(LookupResult{}, /* end_stream (ignored) = */ false);
        return;
    }

    // We need to strip quotes on both sides of the string.
    // TODO: maybe move to redis async client.
    redis_value = redis_value.substr(1, redis_value.length() - 2);

std::cout << "!!!!! data size " << redis_value.length() << "\n";


std::cout << "!!!!! CALLBACK " << "\n";
  //Buffer::OwnedImpl buf;
    //buf.add(redis_value.data(), redis_value.length() + 1);
    //buf.add(redis_value);
    // reassembly proto from received string
    //CacheFileHeader header = makeCacheFileHeaderProto(buf);
  CacheFileHeader header;
  header.ParseFromString(redis_value);

    ASSERT(header.headers().size() != 0);

    // std::cout << "header after parsing: " << header.SerializeAsString() << "\n";

    // get headers from proto.
    Http::ResponseHeaderMapPtr headers = headersFromHeaderProto(header);

    //ResponseMetadata metadata;

    ASSERT(cb_);
    //ASSERT(lookup_);

    // TODO: set length from header block or read it from headers.
    // Here the length is hardcoded to 6.
    /*std::move*/(cb_)(lookup_.makeLookupResult(std::move(headers), metadataFromHeaderProto(header)/*std::move(metadata)*/, 6), false);
    // TODO: get end_stream from header block from redis. 
    ///*std::move*/(cb_)(lookup_.makeLookupResult(std::move(headers), metadataFromHeaderProto(header)/*std::move(metadata)*/, 0), true); -> true does not call getBody.


     
    };
  // TODO: handle here situation when client cannot connect to the redis server.
    // maybe connect it when the first request comes and it is not connected.
    tls_slot_->redis_client_.client_->connect();
  Buffer::OwnedImpl buf;
NetworkFilters::Common::Redis::RespValue request;
  std::vector<NetworkFilters::Common::Redis::RespValue> values(3);
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[0].asString() = "hget";
  values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[1].asString() = absl::StrCat("cache-", stableHashKey(lookup_.key()));
  std::cout << "LOOKING FOR " << absl::StrCat("cache-", stableHashKey(lookup_.key())) << "\n";
  values[2].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[2].asString() = "headers";
  request.type(NetworkFilters::Common::Redis::RespType::Array);
  request.asArray().swap(values);
  tls_slot_->redis_client_.encoder_.encode(request, buf);  

  tls_slot_->redis_client_.client_->write(buf, false);

//auto client = cluster->tcpAsyncClient(nullptr, std::make_shared<const Tcp::AsyncTcpClientOptions>(false));
    
}


void RedisHttpCacheLookupContext::getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb)
{
    //ASSERT(false);

//////
    cb1_ = std::move(cb);
    // allocate thread local tcp async client and wrap it with redis protocol 
    // The client should be allocated and ready to use. Just update the callback.
    // Maybe move callback to a routine called when it is sent ->write(....
    tls_slot_->redis_client_.callback_ = [this] (bool success, std::string redis_value) {

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

    std::cout << redis_value << " length: " << redis_value.length() << "\n";
  std::unique_ptr<Buffer::OwnedImpl> buf;
    buf = std::make_unique<Buffer::OwnedImpl>();
    buf->add(redis_value);
    // call the callback with a result read from redis.
        /*std::move(cb)*/cb1_(std::move(buf), true);

        // TODO: check if the last byte was read and if trailers are present.
#if 0
                      /* end_stream = */ range.end() == header_block_.bodySize() &&
                          header_block_.trailerSize() == 0);
#endif

    // We need to strip quotes on both sides of the string.
    // TODO: maybe move to redis async client.
    };
  // TODO: handle here situation when client cannot connect to the redis server.
    // maybe connect it when the first request comes and it is not connected.
//    tls_slot_->redis_client_.client_->connect();
  Buffer::OwnedImpl buf;
NetworkFilters::Common::Redis::RespValue request;
  std::vector<NetworkFilters::Common::Redis::RespValue> values(4);
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[0].asString() = "getrange";
  values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[1].asString() = absl::StrCat("cache-", stableHashKey(lookup_.key()), "-body");
  //std::cout << "LOOKING FOR " << absl::StrCat("cache-", stableHashKey(lookup_.key())) << "\n";
  values[2].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[2].asString() = fmt::format("{}", range.begin());
  values[3].type(NetworkFilters::Common::Redis::RespType::BulkString);
  // TODO: when it is out of range, success in callback is true and message is 
  // ERR value is not an integer or out of range. How to recognize it as error?
  values[3].asString() = fmt::format("{}", range.begin() + range.length() - 1);
  request.type(NetworkFilters::Common::Redis::RespType::Array);
  request.asArray().swap(values);
  tls_slot_->redis_client_.encoder_.encode(request, buf);  

  std::cout << "RANGE: " << range.begin() << "-" << range.length() << "\n";

  tls_slot_->redis_client_.client_->write(buf, false);

//auto client = cluster->tcpAsyncClient(nullptr, std::make_shared<const Tcp::AsyncTcpClientOptions>(false));
    
//////
}

class RedisHttpCacheInsertContext : public InsertContext, public Logger::Loggable<Logger::Id::cache_filter> {
public:
  RedisHttpCacheInsertContext(std::unique_ptr<RedisHttpCacheLookupContext> lookup_context, 
                    Upstream::ClusterManager& cluster_manager,
ThreadLocal::TypedSlot<RedisHttpCache::ThreadLocalRedisClient>& tls) 
    : lookup_(std::move(lookup_context)), cluster_manager_(cluster_manager), tls_slot_(tls)  {}
  void insertHeaders(const Http::ResponseHeaderMap& /*response_headers*/,
                     const ResponseMetadata& /*metadata*/, InsertCallback /*insert_complete*/,
                     bool /*end_stream*/) override;
  void insertBody(const Buffer::Instance& /*chunk*/, InsertCallback ready_for_next_chunk,
                  bool /*end_stream*/) override; // {ASSERT(false); ready_for_next_chunk(true);}
  void insertTrailers(const Http::ResponseTrailerMap& /*trailers*/,
                      InsertCallback/* insert_complete*/) override {ASSERT(false);}
  void onDestroy() override {/*ASSERT(false);*/}

private:
  // Event::Dispatcher* dispatcher() const;
  // The sequence of actions involved in writing the cache entry to a file. Each
    //LookupContextPtr lookup_;
    std::unique_ptr<RedisHttpCacheLookupContext> lookup_;
    InsertCallback cb_;
    // TODO: can I move cluster_manager to struct stored in threadlocal.
                    Upstream::ClusterManager& cluster_manager_;
                                      ThreadLocal::TypedSlot<RedisHttpCache::ThreadLocalRedisClient>& tls_slot_;
};

void RedisHttpCacheInsertContext::insertHeaders(const Http::ResponseHeaderMap& response_headers,
                     const ResponseMetadata& metadata, InsertCallback cb,
                     bool /*end_stream*/) {
    // Allocate thread local slot, where async client and queue with pending requests are stored.
    auto* cluster = cluster_manager_.getThreadLocalCluster("redis_cluster");
     if (!cluster) {
        ASSERT(false);
    }

    cb_ = std::move(cb);
    // allocate thread local tcp async client and wrap it with redis protocol 
    tls_slot_->redis_client_.client_ = cluster->tcpAsyncClient(nullptr, std::make_shared<const Tcp::AsyncTcpClientOptions>(false));
    tls_slot_->redis_client_.client_->setAsyncTcpClientCallbacks(tls_slot_->redis_client_);
    tls_slot_->redis_client_.callback_ = [this] (bool /*success*/, std::string /*redis_value*/) {

    //ResponseMetadata metadata;

    ASSERT(cb_);
    //ASSERT(lookup_);

 
    // TODO: check what value true means here. What if there is an error talking to redis?
    // Should it be false?
    /*std::move*/(cb_)(true);


     
    };
  // TODO: handle here situation when client cannot connect to the redis server.
    // maybe connect it when the first request comes and it is not connected.
    tls_slot_->redis_client_.client_->connect();
  
  CacheFileHeader header_proto = makeCacheFileHeaderProto(lookup_->key(),
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
  std::vector<NetworkFilters::Common::Redis::RespValue> values(4);
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[0].asString() = "hmset";
  values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[1].asString() = absl::StrCat("cache-", stableHashKey(lookup_->key()));
  values[2].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[2].asString() = "headers";
  values[3].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[3].asString() = header_proto.SerializeAsString();

  request.type(NetworkFilters::Common::Redis::RespType::Array);
  request.asArray().swap(values);
  tls_slot_->redis_client_.encoder_.encode(request, buf);  

  tls_slot_->redis_client_.client_->write(buf, false);

//auto client = cluster->tcpAsyncClient(nullptr, std::make_shared<const Tcp::AsyncTcpClientOptions>(false));
    
}

void RedisHttpCacheInsertContext::insertBody(const Buffer::Instance& chunk, 
                InsertCallback ready_for_next_chunk,
                  bool /*end_stream*/) {
    
    cb_ = std::move(ready_for_next_chunk);
    // TODO: the client should be already connectedt to redis. We should check it here. 
    tls_slot_->redis_client_.callback_ = [this] (bool success, std::string /*redis_value*/) {
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
    /*std::move*/(cb_)(true);
    };


  Buffer::OwnedImpl buf;
NetworkFilters::Common::Redis::RespValue request;
  std::vector<NetworkFilters::Common::Redis::RespValue> values(3);
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[0].asString() = "append";
  values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[1].asString() = absl::StrCat("cache-", stableHashKey(lookup_->key()), "-body");
  values[2].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[2].asString() = chunk.toString();;

  request.type(NetworkFilters::Common::Redis::RespType::Array);
  request.asArray().swap(values);
  tls_slot_->redis_client_.encoder_.encode(request, buf);  

  tls_slot_->redis_client_.client_->write(buf, false);
}



LookupContextPtr RedisHttpCache::makeLookupContext(LookupRequest&& lookup,
                                                        Http::StreamFilterCallbacks& callbacks) {
  return std::make_unique<RedisHttpCacheLookupContext>(callbacks.dispatcher(), *this, redis_client_, cluster_manager_, tls_slot_, std::move(lookup));
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
