#pragma once

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/redis/async_redis_client_impl.h"
#include "source/extensions/filters/http/cache/http_cache.h"
#include "envoy/thread_local/thread_local.h"

#include "envoy/extensions/http/cache/redis_http_cache/v3/redis_http_cache.pb.h"
#include "envoy/extensions/http/cache/redis_http_cache/v3/redis_http_cache.pb.validate.h"
#include "source/extensions/http/cache/redis_http_cache/redis_http_cache_lookup.h"
#include "source/extensions/http/cache/redis_http_cache/redis_http_cache_insert.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {
//namespace {

using ConfigProto = envoy::extensions::http::cache::redis_http_cache::v3::RedisHttpCacheConfig;

//constexpr absl::string_view redis_cluster = "redis_cluster";

class RedisHttpCache : public HttpCache {
public:
#if 0
    struct ThreadLocalRedisClient : public ThreadLocal::ThreadLocalObject {
    ThreadLocalRedisClient() {}
    ~ThreadLocalRedisClient() override {}


    // This really should be hash table of cluster -> redis_client_.
    // The same thread may serve redis cache pointing to several different clusters.
    Extensions::Common::Redis::RedisAsyncClient redis_client_;
    };
#endif
    RedisHttpCache(absl::string_view cluster_name, Upstream::ClusterManager& cluster_manager, ThreadLocal::SlotAllocator& tls) : cluster_name_(cluster_name), cluster_manager_(cluster_manager), tls_slot_(tls) {

    tls_slot_.set([&](Event::Dispatcher&) {return std::make_shared<ThreadLocalRedisClient>(cluster_manager_);});


#if 0
    auto* cluster = cluster_manager_.getThreadLocalCluster("redis_cluster");
     if (!cluster) {
        ASSERT(false);
    }
    redis_client_.client_ = cluster->tcpAsyncClient(nullptr, std::make_shared<const Tcp::AsyncTcpClientOptions>(false));
    redis_client_.client_->setAsyncTcpClientCallbacks(redis_client_);
    redis_client_.callback_ = [/*this*/] (std::string /*redis_value*/) -> void {
std::cout << "!!!!! CALLBACK " << "\n";
    ASSERT(false);
    };
  // TODO: handle here situation when client cannot connect to the redis server.
    // maybe connect it when the first request comes and it is not connected.
    redis_client_.client_->connect();
#endif

#if 0
  Buffer::OwnedImpl buf;
NetworkFilters::Common::Redis::RespValue request;
  std::vector<NetworkFilters::Common::Redis::RespValue> values(2);
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[0].asString() = "get";
  values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[1].asString() = "myheader";
  request.type(NetworkFilters::Common::Redis::RespType::Array);
  request.asArray().swap(values);
  redis_client_.encoder_.encode(request, buf);  

  redis_client_.client_->write(buf, false);
#endif

   }
   LookupContextPtr makeLookupContext(LookupRequest&& /*lookup*/,
                                      Http::StreamFilterCallbacks&/* callbacks*/) override;
   InsertContextPtr makeInsertContext(LookupContextPtr&& /*lookup_context*/,
                                      Http::StreamFilterCallbacks& /*callbacks*/) override; // {return nullptr;}
   CacheInfo cacheInfo() const override {
    CacheInfo info;

    return info;
    }

   void updateHeaders(const LookupContext& /*lookup_context*/,
                      const Http::ResponseHeaderMap& /*response_headers*/,
                      const ResponseMetadata& /*metadata*/,
                      UpdateHeadersCallback /*on_complete*/) override {};

static absl::string_view name() {return "redis cache";}

private:
    //Extensions::Common::Redis::RedisAsyncClient redis_client_;
    std::string cluster_name_;
    Upstream::ClusterManager& cluster_manager_;

    ThreadLocal::TypedSlot<ThreadLocalRedisClient> tls_slot_;

};

//} // namespace
} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
