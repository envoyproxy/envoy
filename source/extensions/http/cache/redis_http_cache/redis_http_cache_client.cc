#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/http/cache/redis_http_cache/redis_http_cache_client.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

    void ThreadLocalRedisClient::send(std::vector<absl::string_view> command, Common::Redis::RedisAsyncClient::ResultCallback&& callback) {


  Buffer::OwnedImpl buf;
NetworkFilters::Common::Redis::RespValue request;
  std::vector<NetworkFilters::Common::Redis::RespValue> values(command.size());

  for (uint32_t i = 0; i < command.size(); i++) {
  values[i].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[i].asString() = command[i];
    }

  request.type(NetworkFilters::Common::Redis::RespType::Array);
  request.asArray().swap(values);
  redis_client_.encoder_.encode(request, buf);  

  redis_client_.write(buf, false, std::move(callback));
}

} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
