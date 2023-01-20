#include "source/common/tcp/async_tcp_client_impl.h"

#include <cstddef>
#include <memory>
#include <utility>

#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/tcp/async_tcp_client.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Tcp {

AsyncTcpClientImpl::AsyncTcpClientImpl(Event::Dispatcher& dispatcher,
                                       Upstream::ThreadLocalCluster& thread_local_cluster,
                                       Upstream::LoadBalancerContext* context,
                                       bool enable_half_close)
    : dispatcher_(dispatcher), thread_local_cluster_(thread_local_cluster), context_(context),
      enable_half_close_(enable_half_close) {}

AsyncTcpClientImpl::~AsyncTcpClientImpl() { close(); }

bool AsyncTcpClientImpl::connect() {
  connection_ = std::move(thread_local_cluster_.tcpConn(context_).connection_);
  if (!connection_) {
    return false;
  }
  connection_->enableHalfClose(enable_half_close_);
  connection_->addConnectionCallbacks(*this);
  connection_->addReadFilter(std::make_shared<NetworkReadFilter>(*this));
  connection_->noDelay(true);
  connection_->connect();
  return true;
}

void AsyncTcpClientImpl::close() {
  if (connection_) {
    connection_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void AsyncTcpClientImpl::addAsyncTcpClientCallbacks(AsyncTcpClientCallbacks& callbacks) {
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

void AsyncTcpClientImpl::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    disconnected_ = true;
    dispatcher_.deferredDelete(std::move(connection_));
    if (callbacks_) {
      callbacks_->onEvent(event);
      callbacks_ = nullptr;
    }
  } else {
    disconnected_ = false;
    if (callbacks_) {
      callbacks_->onEvent(event);
    }
  }
}

} // namespace Tcp
} // namespace Envoy
