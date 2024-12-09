#include "source/extensions/common/redis/async_redis_client_impl.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Redis {

RedisAsyncClient::RedisAsyncClient() : decoder_(*this) {}
void RedisAsyncClient::onEvent(Network::ConnectionEvent /*event*/) {}

void RedisAsyncClient::onData(Buffer::Instance& buf, bool) {
  std::cout << "RECEIVED bytes: " << buf.length() << "\n";

  NetworkFilters::Common::Redis::RespValue response;
  decoder_.decode(buf);
}

void RedisAsyncClient::onRespValue(NetworkFilters::Common::Redis::RespValuePtr&& value) {
  std::cout << value->toString() << "\n";
  if (value->type() == NetworkFilters::Common::Redis::RespType::Null) {
    callback_(std::string("not-found-error"));
  } else {
    callback_(value->toString());
  }

  value.reset();
}

} // namespace Redis
} // namespace Common
} // namespace Extensions
} // namespace Envoy
