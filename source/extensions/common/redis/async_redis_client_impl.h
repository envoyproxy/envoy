#pragma once

#include <queue>
#include "source/common/buffer/buffer_impl.h"
#include "source/common/tcp/async_tcp_client_impl.h"
#include "source/extensions/filters/network/common/redis/codec_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Redis {

class RedisAsyncClient : public Tcp::AsyncTcpClientCallbacks,
                         public NetworkFilters::Common::Redis::DecoderCallbacks {
public:
  using ResultCallback = std::function<void(bool, bool, absl::optional<std::string>)>;
  RedisAsyncClient(Tcp::AsyncTcpClientPtr&& tcp_client, Upstream::ClusterManager&);
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}
  void onData(Buffer::Instance&, bool) override;

  void write(Buffer::Instance& data, bool end_stream, ResultCallback&&);

  // class DecoderCallbacks
  void onRespValue(NetworkFilters::Common::Redis::RespValuePtr&& value) override;

  Tcp::AsyncTcpClientPtr tcp_client_{nullptr};
  // Callback to be called when response from Redis is received.
  ResultCallback callback_;

  NetworkFilters::Common::Redis::EncoderImpl encoder_;
  NetworkFilters::Common::Redis::DecoderImpl decoder_;

  bool waiting_for_response_{false};
  Upstream::ClusterManager& cluster_manager_;
  Upstream::ThreadLocalCluster* cluster_;

  // queue where requests are queued when the async client is currently
  // waiting for a response from the redis server. 
  std::queue<std::tuple<std::unique_ptr<Buffer::OwnedImpl>, bool, ResultCallback>> queue_;
};

} // namespace Redis
} // namespace Common
} // namespace Extensions
} // namespace Envoy
