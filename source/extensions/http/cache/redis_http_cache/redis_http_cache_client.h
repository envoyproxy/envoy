#pragma once

#include "envoy/thread_local/thread_local.h"

#include "source/extensions/common/redis/async_redis_client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

    struct ThreadLocalRedisClient : public ThreadLocal::ThreadLocalObject {
    ThreadLocalRedisClient() {}
    ~ThreadLocalRedisClient() override {}


    // This really should be hash table of cluster -> redis_client_.
    // The same thread may serve redis cache pointing to several different clusters.
    Extensions::Common::Redis::RedisAsyncClient redis_client_;
    void send(std::string command);

    };

// Commands sent to Redis

// Command to read headers block. The result of this query may be:
// - communication error
// error indicating that
// entry does not exist, it means that 

constexpr std::string_view RedisGetHeadersCmd = "get cache-{}";

} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
