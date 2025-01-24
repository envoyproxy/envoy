#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/http/cache/redis_http_cache/redis_http_cache_lookup.h"
#include "source/extensions/http/cache/redis_http_cache/cache_header_proto_util.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

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
    if (redis_value.length() == 4)  {
        redis_value.clear();
    } else {
        redis_value = redis_value.substr(1, redis_value.length() - 2);
    }

    std::cout << "Response from Redis: " << redis_value << ":" << redis_value.length() << "\n";

    // Entry is in redis, but is empty. It means that some other entity
    // is filling the cache.
    if (redis_value.length() == 0) {
        // Entry exists but is empty. It means that some other entity is filling the cache.
        // Continue as if the cache entry was not found.
        LookupResult lookup_result;
        lookup_result.cache_entry_status_ = CacheEntryStatus::LookupError;
        //(cb_)(LookupResult{}, /* end_stream (ignored) = */ false);
        (cb_)(std::move(lookup_result), /* end_stream (ignored) = */ false);
        return;
    }

//std::cout << "!!!!! data size " << redis_value.length() << "\n";


std::cout << "!!!!! CALLBACK " << "\n";
  //Buffer::OwnedImpl buf;
    //buf.add(redis_value.data(), redis_value.length() + 1);
    //buf.add(redis_value);
    // reassembly proto from received string
    //CacheFileHeader header = makeCacheFileHeaderProto(buf);
    
  // CacheFileHeader requires cache_header_proto_util.h to be included.
  CacheFileHeader header;
  header.ParseFromString(redis_value);

    if (header.headers().size() == 0) {
        std::cout << "Nothing found in the database.\n";
        // TODO: end_stream should be taken based on info from cache.

        //(cb_)(LookupResult{}, /* end_stream (ignored) = */ true); true -> will not call getBody.
        (cb_)(LookupResult{}, /* end_stream (ignored) = */ false);
        return;
    }

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
#if 0
  Buffer::OwnedImpl buf;
NetworkFilters::Common::Redis::RespValue request;
  std::vector<NetworkFilters::Common::Redis::RespValue> values(2);
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[0].asString() = "get";
  values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[1].asString() = absl::StrCat("cache-", stableHashKey(lookup_.key()));
  std::cout << "LOOKING FOR " << absl::StrCat("cache-", stableHashKey(lookup_.key())) << "\n";
  //values[2].type(NetworkFilters::Common::Redis::RespType::BulkString);
  //values[2].asString() = "headers";
  request.type(NetworkFilters::Common::Redis::RespType::Array);
  request.asArray().swap(values);
  tls_slot_->redis_client_.encoder_.encode(request, buf);  

  tls_slot_->redis_client_.client_->write(buf, false);
#endif
  tls_slot_->send(fmt::format(RedisGetHeadersCmd, stableHashKey(lookup_.key())));

//auto client = cluster->tcpAsyncClient(nullptr, std::make_shared<const Tcp::AsyncTcpClientOptions>(false));
}
    
} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
