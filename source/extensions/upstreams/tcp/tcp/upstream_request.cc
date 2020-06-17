#include "extensions/upstreams/tcp/tcp/upstream_request.h"

#include <memory>

#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Tcp {
namespace Tcp {
TcpUpstream::TcpUpstream(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& data,
                         Envoy::Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks)
    : upstream_conn_data_(std::move(data)) {
  Network::ClientConnection& connection = upstream_conn_data_->connection();
  connection.enableHalfClose(true);
  upstream_conn_data_->addUpstreamCallbacks(upstream_callbacks);
}

bool TcpUpstream::readDisable(bool disable) {
  if (upstream_conn_data_ == nullptr ||
      upstream_conn_data_->connection().state() != Network::Connection::State::Open) {
    // Because we flush write downstream, we can have a case where upstream has already disconnected
    // and we are waiting to flush. If we had a watermark event during this time we should no
    // longer touch the upstream connection.
    return false;
  }

  upstream_conn_data_->connection().readDisable(disable);
  return true;
}

void TcpUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  upstream_conn_data_->connection().write(data, end_stream);
}

void TcpUpstream::addBytesSentCallback(Network::Connection::BytesSentCb cb) {
  upstream_conn_data_->connection().addBytesSentCallback(cb);
}

Envoy::Tcp::ConnectionPool::ConnectionData*
TcpUpstream::onDownstreamEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose) {
    // The close call may result in this object being deleted. Latch the
    // connection locally so it can be returned for potential draining.
    auto* conn_data = upstream_conn_data_.release();
    conn_data->connection().close(Network::ConnectionCloseType::FlushWrite);
    return conn_data;
  } else if (event == Network::ConnectionEvent::LocalClose) {
    upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
  }
  return nullptr;
}

void TcpConnectionHandle::onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                                      Upstream::HostDescriptionConstSharedPtr host) {
  Network::Connection& latched_conn = conn_data->connection();
  tcp_upstream_ = std::make_shared<TcpUpstream>(std::move(conn_data), tcp_upstream_callbacks_);
  // Disable cancel.
  tcp_upstream_handle_ = nullptr;
  generic_pool_callbacks_.onPoolReady(tcp_upstream_, host, latched_conn.localAddress(),
                                      latched_conn.streamInfo());
}

void TcpConnectionHandle::onPoolFailure(Envoy::Tcp::ConnectionPool::PoolFailureReason reason,
                                        Upstream::HostDescriptionConstSharedPtr host) {
  // An immediate callback from conn pool. tcp_upstream_handle_ could indicate successful pool
  // ready. However, it's impossible in the context of onPoolFailure.
  if (tcp_upstream_handle_ == nullptr) {
    has_failure_ = true;
  } else {
    // The onPoolFailure is invoked outside the scope of handle creation. Trigger onPoolFailure.
    generic_pool_callbacks_.onPoolFailure(reason, host);
  }
}
} // namespace Tcp
} // namespace Tcp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy