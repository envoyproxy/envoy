#include "source/common/tcp/conn_pool.h"

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/upstream/upstream.h"

#include "source/common/stats/timespan_impl.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Tcp {

ActiveTcpClient::ActiveTcpClient(Envoy::ConnectionPool::ConnPoolImplBase& parent,
                                 const Upstream::HostConstSharedPtr& host,
                                 uint64_t concurrent_stream_limit,
                                 absl::optional<std::chrono::milliseconds> idle_timeout)
    : Envoy::ConnectionPool::ActiveClient(parent, host->cluster().maxRequestsPerConnection(),
                                          concurrent_stream_limit),
      parent_(parent), idle_timeout_(idle_timeout) {
  Upstream::Host::CreateConnectionData data = host->createConnection(
      parent_.dispatcher(), parent_.socketOptions(), parent_.transportSocketOptions());
  real_host_description_ = data.host_description_;
  connection_ = std::move(data.connection_);
  connection_->addConnectionCallbacks(*this);
  read_filter_handle_ = std::make_shared<ConnReadFilter>(*this);
  connection_->addReadFilter(read_filter_handle_);
  Upstream::ClusterTrafficStats& cluster_traffic_stats = *host->cluster().trafficStats();
  connection_->setConnectionStats({cluster_traffic_stats.upstream_cx_rx_bytes_total_,
                                   cluster_traffic_stats.upstream_cx_rx_bytes_buffered_,
                                   cluster_traffic_stats.upstream_cx_tx_bytes_total_,
                                   cluster_traffic_stats.upstream_cx_tx_bytes_buffered_,
                                   &cluster_traffic_stats.bind_errors_, nullptr});
  connection_->noDelay(true);
  connection_->connect();

  if (idle_timeout_.has_value()) {
    idle_timer_ = connection_->dispatcher().createTimer([this]() -> void { onIdleTimeout(); });
    setIdleTimer();
  }
}

ActiveTcpClient::~ActiveTcpClient() {
  // Handle the case where deferred delete results in the ActiveClient being destroyed before
  // TcpConnectionData. Make sure the TcpConnectionData will not refer to this ActiveTcpClient
  // and handle clean up normally done in clearCallbacks()
  if (tcp_connection_data_) {
    ASSERT(state() == ActiveClient::State::Closed);
    tcp_connection_data_->release();
    parent_.onStreamClosed(*this, true);
    parent_.checkForIdleAndCloseIdleConnsIfDraining();
  }
}

void ActiveTcpClient::close() { connection_->close(Network::ConnectionCloseType::NoFlush); }

void ActiveTcpClient::clearCallbacks() {
  if (state() == Envoy::ConnectionPool::ActiveClient::State::Busy && parent_.hasPendingStreams()) {
    auto* pool = &parent_;
    pool->scheduleOnUpstreamReady();
  }
  callbacks_ = nullptr;
  tcp_connection_data_ = nullptr;
  parent_.onStreamClosed(*this, true);
  setIdleTimer();
  parent_.checkForIdleAndCloseIdleConnsIfDraining();
}

void ActiveTcpClient::onEvent(Network::ConnectionEvent event) {
  // If this is a newly established TCP connection, readDisable. This is to handle a race condition
  // for TCP for protocols like MySQL where the upstream writes first, and the data needs to be
  // preserved until a downstream connection is associated.
  // This is also necessary for prefetch to be used with such protocols.
  if (event == Network::ConnectionEvent::Connected) {
    connection_->readDisable(true);
  }
  ENVOY_BUG(event != Network::ConnectionEvent::ConnectedZeroRtt,
            "Unexpected 0-RTT event from the underlying TCP connection.");
  parent_.onConnectionEvent(*this, connection_->transportFailureReason(), event);

  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    disableIdleTimer();

    // Do not pass the Connected event to any session which registered during onEvent above.
    // Consumers of connection pool connections assume they are receiving already connected
    // connections.
    if (callbacks_) {
      if (tcp_connection_data_) {
        Envoy::Upstream::reportUpstreamCxDestroyActiveRequest(parent_.host(), event);
      }
      callbacks_->onEvent(event);
      // After receiving a disconnect event, the owner of callbacks_ will likely self-destruct.
      // Clear the pointer to avoid using it again.
      callbacks_ = nullptr;
    }
  }
}

void ActiveTcpClient::onIdleTimeout() {
  ENVOY_CONN_LOG(debug, "per client idle timeout", *connection_);
  parent_.host()->cluster().trafficStats()->upstream_cx_idle_timeout_.inc();
  close();
}

void ActiveTcpClient::disableIdleTimer() {
  if (idle_timer_ != nullptr) {
    idle_timer_->disableTimer();
  }
}

void ActiveTcpClient::setIdleTimer() {
  if (idle_timer_ != nullptr) {
    ASSERT(idle_timeout_.has_value());

    idle_timer_->enableTimer(idle_timeout_.value());
  }
}

} // namespace Tcp
} // namespace Envoy
