#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/http/cache/redis_http_cache/redis_http_cache_client.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

    void ThreadLocalRedisClient::send(std::string command) {

    std::vector<std::string_view> v = absl::StrSplit(command, " ");

  Buffer::OwnedImpl buf;
NetworkFilters::Common::Redis::RespValue request;
  std::vector<NetworkFilters::Common::Redis::RespValue> values(v.size());

  for (uint32_t i = 0; i < v.size(); i++) {
  values[i].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[i].asString() = v[i];
    }

  request.type(NetworkFilters::Common::Redis::RespType::Array);
  request.asArray().swap(values);
  redis_client_.encoder_.encode(request, buf);  

  redis_client_.client_->write(buf, false);
    }


} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
