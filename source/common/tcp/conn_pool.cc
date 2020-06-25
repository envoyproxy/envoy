#include "common/tcp/conn_pool.h"

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/upstream/upstream.h"

#include "common/stats/timespan_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Tcp {

ActiveTcpClient::ActiveTcpClient(ConnPoolImpl& parent, uint64_t lifetime_request_limit,
                                 uint64_t concurrent_request_limit)
    : Envoy::ConnectionPool::ActiveClient(parent, lifetime_request_limit, concurrent_request_limit),
      parent_(parent) {
  Upstream::Host::CreateConnectionData data = parent_.host_->createConnection(
      parent_.dispatcher_, parent_.socket_options_, parent_.transport_socket_options_);
  real_host_description_ = data.host_description_;
  connection_ = std::move(data.connection_);
  connection_->addConnectionCallbacks(*this);
  connection_->detectEarlyCloseWhenReadDisabled(false);
  connection_->addReadFilter(Network::ReadFilterSharedPtr{new ConnReadFilter(*this)});
  connection_->connect();
}

void ActiveTcpClient::clearCallbacks() {
  if (state_ == Envoy::ConnectionPool::ActiveClient::State::BUSY ||
      state_ == Envoy::ConnectionPool::ActiveClient::State::DRAINING) {
    parent_.onConnReleased(*this);
  }
  callbacks_ = nullptr;
  tcp_connection_data_ = nullptr;
  parent_.onRequestClosed(*this, true);
  parent_.checkForDrained();
}

ActiveTcpClient::~ActiveTcpClient() {
  // Handle the case where deferred delete results in the ActiveClient being destroyed before
  // TcpConnectionData. Make sure the TcpConnectionData will not refer to this ActiveTcpClient
  // and handle clean up normally done in clearCallbacks()
  if (tcp_connection_data_) {
    ASSERT(state_ == ActiveClient::State::CLOSED);
    tcp_connection_data_->release();
    parent_.onRequestClosed(*this, true);
    parent_.checkForDrained();
  }
  parent_.onConnDestroyed();
}

void ActiveTcpClient::onEvent(Network::ConnectionEvent event) {
  Envoy::ConnectionPool::ActiveClient::onEvent(event);
  if (callbacks_ && event != Network::ConnectionEvent::Connected) {
    callbacks_->onEvent(event);
    callbacks_ = nullptr;
  }
}

} // namespace Tcp
} // namespace Envoy
