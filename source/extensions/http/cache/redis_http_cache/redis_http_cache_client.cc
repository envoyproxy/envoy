#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/http/cache/redis_http_cache/redis_http_cache_client.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

    //void ThreadLocalRedisClient::send(std::vector<absl::string_view> command, Common::Redis::RedisAsyncClient::ResultCallback&& callback) {
    //absl::flat_hash_map<std::string, Extensions::Common::Redis::RedisAsyncClient> redis_client_;

    bool ThreadLocalRedisClient::send(absl::string_view cluster_name, std::vector<absl::string_view> command, Extensions::Common::Redis::RedisAsyncClient::ResultCallback&& callback) {


    //locate the cluster
    auto redis_client = redis_client_.find(cluster_name);

    if (redis_client == redis_client_.end()) {
        auto cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
        if (!cluster) {
            ENVOY_LOG_MISC(trace, "Cannot find cluster: {}", cluster_name);
       //     callback(false, false, absl::nullopt);
            return false;
        }
        //redis_client_.emplace(cluster,Extensions::Common::Redis::RedisAsyncClient(cluster_manager_));
    auto tcp_client = cluster->tcpAsyncClient(nullptr, std::make_shared<const Tcp::AsyncTcpClientOptions>(false));
        redis_client_.emplace(cluster_name, std::make_unique<Extensions::Common::Redis::RedisAsyncClient>(std::move(tcp_client), cluster_manager_));
        redis_client = redis_client_.find(cluster_name);
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

  //redis_client_.write(buf, false, std::move(callback));
  redis_client->second->write(buf, false, std::move(callback));

    return true;
}

} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
