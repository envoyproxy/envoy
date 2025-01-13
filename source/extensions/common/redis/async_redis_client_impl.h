#pragma once

#include "source/common/tcp/async_tcp_client_impl.h"
#include "source/extensions/filters/network/common/redis/codec_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Redis {

class RedisAsyncClient : public Tcp::AsyncTcpClientCallbacks,
                         public NetworkFilters::Common::Redis::DecoderCallbacks {
  // RedisAsyncClient(Tcp::AsyncTcpClientPtr tcp_client) : client_(tcp_client) {}
public:
  RedisAsyncClient();
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}
  void onData(Buffer::Instance&, bool) override;

  // class DecoderCallbacks
  void onRespValue(NetworkFilters::Common::Redis::RespValuePtr&& value) override;

  Tcp::AsyncTcpClientPtr client_;
  // callback registered by user of RedisAsyncClient.
  std::function<void(bool, std::string)> callback_;

  NetworkFilters::Common::Redis::EncoderImpl encoder_;
  NetworkFilters::Common::Redis::DecoderImpl decoder_;
};

} // namespace Redis
} // namespace Common
} // namespace Extensions
} // namespace Envoy
