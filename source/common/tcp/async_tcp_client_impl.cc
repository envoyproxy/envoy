#include "source/common/tcp/async_tcp_client_impl.h"

#include <cstddef>
#include <memory>
#include <utility>

#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/tcp/async_tcp_client.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/stats/timespan_impl.h"

namespace Envoy {
namespace Tcp {

AsyncTcpClientImpl::AsyncTcpClientImpl(Event::Dispatcher& dispatcher,
                                       Upstream::ThreadLocalCluster& thread_local_cluster,
                                       Upstream::LoadBalancerContext* context,
                                       bool enable_half_close)
    : dispatcher_(dispatcher), thread_local_cluster_(thread_local_cluster),
      cluster_info_(thread_local_cluster_.info()), context_(context),
      connect_timer_(dispatcher.createTimer([this]() { onConnectTimeout(); })),
      enable_half_close_(enable_half_close) {
  connect_timer_->enableTimer(cluster_info_->connectTimeout());
  cluster_info_->trafficStats()->upstream_cx_active_.inc();
  cluster_info_->trafficStats()->upstream_cx_total_.inc();
}

AsyncTcpClientImpl::~AsyncTcpClientImpl() {
  cluster_info_->trafficStats()->upstream_cx_active_.dec();
}

bool AsyncTcpClientImpl::connect() {
  connection_ = std::move(thread_local_cluster_.tcpConn(context_).connection_);
  if (!connection_) {
    return false;
  }
  connection_->enableHalfClose(enable_half_close_);
  connection_->addConnectionCallbacks(*this);
  connection_->addReadFilter(std::make_shared<NetworkReadFilter>(*this));
  conn_connect_ms_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      cluster_info_->trafficStats()->upstream_cx_connect_ms_, dispatcher_.timeSource());

  connect_timer_->enableTimer(cluster_info_->connectTimeout());
  connection_->setConnectionStats({cluster_info_->trafficStats()->upstream_cx_rx_bytes_total_,
                                   cluster_info_->trafficStats()->upstream_cx_rx_bytes_buffered_,
                                   cluster_info_->trafficStats()->upstream_cx_tx_bytes_total_,
                                   cluster_info_->trafficStats()->upstream_cx_tx_bytes_buffered_,
                                   &cluster_info_->trafficStats()->bind_errors_, nullptr});
  connection_->noDelay(true);
  connection_->connect();
  return true;
}

void AsyncTcpClientImpl::onConnectTimeout() {
  ENVOY_CONN_LOG(debug, "async tcp connection timed out", *connection_);
  cluster_info_->trafficStats()->upstream_cx_connect_timeout_.inc();
  close(Network::ConnectionCloseType::NoFlush);
}

void AsyncTcpClientImpl::close(Network::ConnectionCloseType type) {
  if (connection_) {
    connection_->close(type);
  }
}

void AsyncTcpClientImpl::setAsyncTcpClientCallbacks(AsyncTcpClientCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

void AsyncTcpClientImpl::write(Buffer::Instance& data, bool end_stream) {
  ASSERT(connection_ != nullptr);
  connection_->write(data, end_stream);
}

void AsyncTcpClientImpl::onData(Buffer::Instance& data, bool end_stream) {
  if (callbacks_) {
    callbacks_->onData(data, end_stream);
  }
}

void AsyncTcpClientImpl::disableConnectTimeout() {
  if (connect_timer_) {
    connect_timer_->disableTimer();
    connect_timer_.reset();
  }
}

void AsyncTcpClientImpl::reportConnectionDestroy(Network::ConnectionEvent event) {
  auto& stats = cluster_info_->trafficStats();
  stats->upstream_cx_destroy_.inc();
  if (event == Network::ConnectionEvent::RemoteClose) {
    stats->upstream_cx_destroy_remote_.inc();
  } else {
    stats->upstream_cx_destroy_local_.inc();
  }
}

void AsyncTcpClientImpl::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    if (disconnected_) {
      cluster_info_->trafficStats()->upstream_cx_connect_fail_.inc();
    }

    if (!disconnected_ && conn_length_ms_ != nullptr) {
      conn_length_ms_->complete();
      conn_length_ms_.reset();
    }
    disableConnectTimeout();
    reportConnectionDestroy(event);
    disconnected_ = true;
    if (connection_) {
      detected_close_ = connection_->detectedCloseType();
    }

    dispatcher_.deferredDelete(std::move(connection_));
    if (callbacks_) {
      callbacks_->onEvent(event);
      callbacks_ = nullptr;
    }
  } else {
    disconnected_ = false;
    conn_connect_ms_->complete();
    conn_connect_ms_.reset();
    disableConnectTimeout();

    if (!conn_length_ms_) {
      conn_length_ms_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
          cluster_info_->trafficStats()->upstream_cx_length_ms_, dispatcher_.timeSource());
    }
    if (callbacks_) {
      callbacks_->onEvent(event);
    }
  }
}

} // namespace Tcp
} // namespace Envoy
