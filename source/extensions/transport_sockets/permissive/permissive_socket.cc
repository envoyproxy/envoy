#include "extensions/transport_sockets/permissive/permissive_socket.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Permissive {

PermissiveSocket::PermissiveSocket(Network::TransportSocketPtr&& primary_transport_socket,
                                   Network::TransportSocketPtr&& secondary_transport_socket)
    : primary_transport_socket_(std::move(primary_transport_socket)),
      secondary_transport_socket_(std::move(secondary_transport_socket)) {}

void PermissiveSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  callbacks_ = std::make_unique<ProxyTransportSocketCallbacks>(callbacks);

  if (is_fallback_) {
    secondary_transport_socket_->setTransportSocketCallbacks(*callbacks_);
  } else {
    primary_transport_socket_->setTransportSocketCallbacks(*callbacks_);
  }
}

Network::IoResult PermissiveSocket::doRead(Buffer::Instance& buffer) {
  if (is_fallback_) {
    return secondary_transport_socket_->doRead(buffer);
  } else {
    callbacks_->clearEvents();
    Network::IoResult io_result = primary_transport_socket_->doRead(buffer);
    checkIoResult(io_result);
    return io_result;
  }
}

Network::IoResult PermissiveSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  if (is_fallback_) {
    return secondary_transport_socket_->doWrite(buffer, end_stream);
  } else {
    callbacks_->clearEvents();
    Network::IoResult io_result = primary_transport_socket_->doWrite(buffer, end_stream);
    checkIoResult(io_result);
    return io_result;
  }
}

std::string PermissiveSocket::protocol() const {
  if (is_fallback_) {
    return secondary_transport_socket_->protocol();
  } else {
    return primary_transport_socket_->protocol();
  }
}

absl::string_view PermissiveSocket::failureReason() const {
  if (is_fallback_) {
    return secondary_transport_socket_->failureReason();
  } else {
    return primary_transport_socket_->failureReason();
  }
}

void PermissiveSocket::onConnected() {
  if (is_fallback_) {
    secondary_transport_socket_->onConnected();
  } else {
    primary_transport_socket_->onConnected();
  }
}

bool PermissiveSocket::canFlushClose() {
  if (is_fallback_) {
    return secondary_transport_socket_->canFlushClose();
  } else {
    return primary_transport_socket_->canFlushClose();
  }
}

void PermissiveSocket::closeSocket(Network::ConnectionEvent event) {
  if (is_fallback_) {
    secondary_transport_socket_->closeSocket(event);
  } else {
    primary_transport_socket_->closeSocket(event);
  }
}

const Ssl::ConnectionInfo* PermissiveSocket::ssl() const {
  if (is_fallback_) {
    return secondary_transport_socket_->ssl();
  } else {
    return primary_transport_socket_->ssl();
  }
}

void PermissiveSocket::checkIoResult(Network::IoResult& io_result) {
  ASSERT(!is_fallback_);

  /**
   * Check if the event Network::ConnectionEvent::Connected was raised.
   * Check if the action is Network::PostIoAction::Close.
   */
  if (!callbacks_->eventRaised(Network::ConnectionEvent::Connected) &&
      io_result.action_ == Network::PostIoAction::Close) {
    // TODO(crazyxy): add metrics
    is_fallback_ = true;
    ENVOY_CONN_LOG(trace, "Transport socket fallback", callbacks_->connection());

    // The underlying TCP connection is supposed to be closed. Raise the event to rebuild the TCP
    // connection.
    io_result.action_ = Network::PostIoAction::Reconnect;
  }
}

Network::TransportSocketPtr PermissiveSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsSharedPtr options) const {
  return std::make_unique<PermissiveSocket>(
      primary_transport_socket_factory_->createTransportSocket(options),
      secondary_transport_socket_factory_->createTransportSocket(options));
}

bool PermissiveSocketFactory::implementsSecureTransport() const { return false; }

} // namespace Permissive
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
