#pragma once

#include <chrono>
#include <cstddef>
#include <string>

#include "envoy/common/optref.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/timespan.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tcp/async_tcp_client.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/network/filter_impl.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Tcp {

class AsyncTcpClientImpl : public AsyncTcpClient,
                           public Network::ConnectionCallbacks,
                           public Logger::Loggable<Logger::Id::client> {
public:
  AsyncTcpClientImpl(Event::Dispatcher& dispatcher,
                     Upstream::ThreadLocalCluster& thread_local_cluster,
                     Upstream::LoadBalancerContext* context, bool enable_half_close);
  ~AsyncTcpClientImpl();

  void close(Network::ConnectionCloseType type) override;

  Network::DetectedCloseType detectedCloseType() const override { return detected_close_; }

  /**
   * @return true means a host is successfully picked from a Cluster.
   * This doesn't mean the connection is established.
   */
  bool connect() override;

  void onConnectTimeout();

  void setAsyncTcpClientCallbacks(AsyncTcpClientCallbacks& callbacks) override;

  void write(Buffer::Instance& data, bool end_stream) override;

  void readDisable(bool disable) override {
    if (connection_) {
      connection_->readDisable(disable);
    }
  };

  /**
   * @return if the client connects to a peer host.
   */
  bool connected() override { return connected_; }

  Event::Dispatcher& dispatcher() override { return dispatcher_; }

  OptRef<StreamInfo::StreamInfo> getStreamInfo() override {
    if (connection_) {
      return connection_->streamInfo();
    } else {
      return absl::nullopt;
    }
  }

private:
  struct NetworkReadFilter : public Network::ReadFilterBaseImpl {
    NetworkReadFilter(AsyncTcpClientImpl& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
      parent_.onData(data, end_stream);
      return Network::FilterStatus::StopIteration;
    }

    AsyncTcpClientImpl& parent_;
  };

  void onData(Buffer::Instance& data, bool end_stream);

  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {
    if (callbacks_) {
      callbacks_->onAboveWriteBufferHighWatermark();
    }
  }
  void onBelowWriteBufferLowWatermark() override {
    if (callbacks_) {
      callbacks_->onBelowWriteBufferLowWatermark();
    }
  }

  void disableConnectTimeout();
  void reportConnectionDestroy(Network::ConnectionEvent event);

  Event::Dispatcher& dispatcher_;
  Network::ClientConnectionPtr connection_;
  Upstream::ThreadLocalCluster& thread_local_cluster_;
  Upstream::ClusterInfoConstSharedPtr cluster_info_;
  Upstream::LoadBalancerContext* context_;
  Stats::TimespanPtr conn_connect_ms_;
  Stats::TimespanPtr conn_length_ms_;
  Event::TimerPtr connect_timer_;
  AsyncTcpClientCallbacks* callbacks_{};
  Network::DetectedCloseType detected_close_{Network::DetectedCloseType::Normal};
  bool closing_{false};
  bool connected_{false};
  bool enable_half_close_{false};
};

} // namespace Tcp
} // namespace Envoy
