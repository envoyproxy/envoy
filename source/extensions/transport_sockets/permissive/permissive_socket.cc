#include "extensions/transport_sockets/permissive/permissive_socket.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Permissive {

PermissiveSocket::PermissiveSocket(Network::TransportSocketPtr&& tls_transport_socket,
                                   Network::TransportSocketPtr&& raw_buffer_transport_socket)
    : tls_transport_socket_(std::move(tls_transport_socket)),
      raw_buffer_transport_socket_(std::move(raw_buffer_transport_socket)) {}

void PermissiveSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
  if (downgraded_) {
    raw_buffer_transport_socket_->setTransportSocketCallbacks(callbacks);
  } else {
    tls_transport_socket_->setTransportSocketCallbacks(callbacks);
  }
}

Network::IoResult PermissiveSocket::doRead(Buffer::Instance& buffer) {
  if (downgraded_) {
    return raw_buffer_transport_socket_->doRead(buffer);
  } else {
    Network::IoResult io_result = tls_transport_socket_->doRead(buffer);
    checkIoResult(io_result);
    return io_result;
  }
}

Network::IoResult PermissiveSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  if (downgraded_) {
    return raw_buffer_transport_socket_->doWrite(buffer, end_stream);
  } else {
    Network::IoResult io_result = tls_transport_socket_->doWrite(buffer, end_stream);
    checkIoResult(io_result);
    return io_result;
  }
}

std::string PermissiveSocket::protocol() const {
  if (downgraded_) {
    return raw_buffer_transport_socket_->protocol();
  } else {
    return tls_transport_socket_->protocol();
  }
}

absl::string_view PermissiveSocket::failureReason() const {
  if (downgraded_) {
    return raw_buffer_transport_socket_->failureReason();
  } else {
    return tls_transport_socket_->failureReason();
  }
}

void PermissiveSocket::onConnected() {
  if (downgraded_) {
    raw_buffer_transport_socket_->onConnected();
  } else {
    tls_transport_socket_->onConnected();
  }
}

bool PermissiveSocket::canFlushClose() {
  if (downgraded_) {
    return raw_buffer_transport_socket_->canFlushClose();
  } else {
    return tls_transport_socket_->canFlushClose();
  }
}

void PermissiveSocket::closeSocket(Network::ConnectionEvent event) {
  if (downgraded_) {
    raw_buffer_transport_socket_->closeSocket(event);
  } else {
    tls_transport_socket_->closeSocket(event);
  }
}

const Ssl::ConnectionInfo* PermissiveSocket::ssl() const {
  if (downgraded_) {
    return raw_buffer_transport_socket_->ssl();
  } else {
    return tls_transport_socket_->ssl();
  }
}

void PermissiveSocket::checkIoResult(Network::IoResult& io_result) {
  ASSERT(!downgraded_);

  /**
   * The function is for checking if the TLS handshake succeeded or not. If handshake failed,
   * fallback to plaintext connection.
   *  * check handshake_complete_. If handshake_complete_ is true, it means handshake
   * completed. Since handshake_complete_ is private in ssl_socket, we can call canFlushClose()
   * instead.
   *  * check if the action is Network::PostIoAction::Close.
   */
  if (!canFlushClose() && io_result.action_ == Network::PostIoAction::Close) {
    // TODO(crazyxy): add metrics
    downgraded_ = true;
    ENVOY_CONN_LOG(trace, "TLS connection downgrade to plaintext", callbacks_->connection());

    // The underlying TCP connection is supposed to be closed. Raise the event to rebuild the TCP
    // connection.
    io_result.action_ = Network::PostIoAction::Reconnect;
  }
}

Network::TransportSocketPtr PermissiveSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsSharedPtr options) const {
  return std::make_unique<PermissiveSocket>(
      tls_transport_socket_factory_->createTransportSocket(options),
      raw_buffer_transport_socket_facotry_->createTransportSocket(options));
}

bool PermissiveSocketFactory::implementsSecureTransport() const { return false; }

} // namespace Permissive
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
