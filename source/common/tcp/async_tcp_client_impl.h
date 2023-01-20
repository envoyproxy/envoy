#pragma once

#include <chrono>
#include <cstddef>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/tcp/async_tcp_client.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/network/filter_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Tcp {

class AsyncTcpClientImpl : public AsyncTcpClient, public Network::ConnectionCallbacks {
public:
  AsyncTcpClientImpl(Event::Dispatcher& dispatcher,
                     Upstream::ThreadLocalCluster& thread_local_cluster,
                     Upstream::LoadBalancerContext* context, bool enable_half_close);

  ~AsyncTcpClientImpl() override;

  void close() override;

  /**
   * @return true means a host is successfully picked from a Cluster.
   * This doesn't mean the connection is established.
   */
  bool connect() override;

  void addAsyncTcpClientCallbacks(AsyncTcpClientCallbacks& callbacks) override;

  void write(Buffer::Instance& data, bool end_stream) override;

  void readDisable(bool disable) override {
    if (connection_) {
      connection_->readDisable(disable);
    }
  };

  /**
   * @return true means a host is successfully picked from a Cluster.
   * This doesn't mean the connection is established. It should be checked after
   * calling connect().
   */

  /**
   * @return if the client connects to a peer host.
   */
  bool connected() override { return !disconnected_; }

  Event::Dispatcher& dispatcher() override { return dispatcher_; }

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
  void onAboveWriteBufferHighWatermark() override { callbacks_->onAboveWriteBufferHighWatermark(); }
  void onBelowWriteBufferLowWatermark() override { callbacks_->onBelowWriteBufferLowWatermark(); }

  Event::Dispatcher& dispatcher_;
  Network::ClientConnectionPtr connection_;
  Upstream::ThreadLocalCluster& thread_local_cluster_;
  Upstream::LoadBalancerContext* context_;
  AsyncTcpClientCallbacks* callbacks_{};
  bool disconnected_{true};
  bool enable_half_close_{false};
};

} // namespace Tcp
} // namespace Envoy
