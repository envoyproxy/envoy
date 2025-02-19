#pragma once

#include "envoy/thread_local/thread_local.h"

#include "source/extensions/common/redis/async_redis_client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

    struct ThreadLocalRedisClient : public ThreadLocal::ThreadLocalObject {
    ThreadLocalRedisClient(Upstream::ClusterManager& cluster_manager) : redis_client_(cluster_manager) {}
    ~ThreadLocalRedisClient() override {}


    // This really should be hash table of cluster -> redis_client_.
    // The same thread may serve redis cache pointing to several different clusters.
    Extensions::Common::Redis::RedisAsyncClient redis_client_;

    void send(std::vector<absl::string_view> command, Common::Redis::RedisAsyncClient::ResultCallback&& callback);

    };

constexpr std::string_view RedisCacheHeadersEntry = "cache-{}-headers";
constexpr std::string_view RedisCacheBodyEntry = "cache-{}-body";
constexpr std::string_view RedisCacheTrailersEntry = "cache-{}-trailers";

} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
