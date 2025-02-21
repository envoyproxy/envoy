#pragma once

#include "envoy/thread_local/thread_local.h"

#include "source/common/upstream/cluster_manager_impl.h"
#include "source/extensions/common/redis/async_redis_client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

struct ThreadLocalRedisClient : public ThreadLocal::ThreadLocalObject {
  ThreadLocalRedisClient(Upstream::ClusterManager& cluster_manager)
      : cluster_manager_(cluster_manager) {}
  ~ThreadLocalRedisClient() override {}

  absl::flat_hash_map<std::string, std::unique_ptr<Extensions::Common::Redis::RedisAsyncClient>>
      redis_client_;

  bool send(absl::string_view cluster, std::vector<absl::string_view> command,
            Extensions::Common::Redis::RedisAsyncClient::ResultCallback&& callback);

  Upstream::ClusterManager& cluster_manager_;
};

constexpr std::string_view RedisCacheHeadersEntry = "cache-{}-headers";
constexpr std::string_view RedisCacheBodyEntry = "cache-{}-body";
constexpr std::string_view RedisCacheTrailersEntry = "cache-{}-trailers";

} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
