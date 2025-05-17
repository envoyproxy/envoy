#include "source/extensions/http/cache/redis_http_cache/redis_http_cache_client.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

bool ThreadLocalRedisClient::send(
    absl::string_view cluster_name, std::vector<absl::string_view> command,
    Extensions::Common::Redis::RedisAsyncClient::ResultCallback&& callback) {

  // locate the cluster
  auto redis_client = redis_clients_.find(cluster_name);

  if (redis_client == redis_clients_.end()) {
    auto cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
    if (!cluster) {
      ENVOY_LOG_MISC(trace, "Cannot find cluster: {}", cluster_name);
      return false;
    }
    auto tcp_client =
        cluster->tcpAsyncClient(nullptr, std::make_shared<const Tcp::AsyncTcpClientOptions>(false));
    redis_clients_.emplace(cluster_name,
                           std::make_unique<Extensions::Common::Redis::RedisAsyncClient>(
                               std::move(tcp_client), cluster_manager_));
    redis_client = redis_clients_.find(cluster_name);
  }

  Buffer::OwnedImpl buf;
  NetworkFilters::Common::Redis::RespValue request;
  std::vector<NetworkFilters::Common::Redis::RespValue> values(command.size());

  for (uint32_t i = 0; i < command.size(); i++) {
    values[i].type(NetworkFilters::Common::Redis::RespType::BulkString);
    values[i].asString() = command[i];
  }

  request.type(NetworkFilters::Common::Redis::RespType::Array);
  request.asArray().swap(values);
  redis_client->second->encoder_.encode(request, buf);

  redis_client->second->write(buf, false, std::move(callback));

  return true;
}

} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
