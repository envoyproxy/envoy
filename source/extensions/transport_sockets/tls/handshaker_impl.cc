#include "extensions/transport_sockets/tls/handshaker_impl.h"

#include "envoy/network/connection.h"
#include "envoy/ssl/handshaker.h"
#include "envoy/ssl/socket_state.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

using Envoy::Ssl::SocketState;
using Network::PostIoAction;

PostIoAction HandshakerImpl::doHandshake(SocketState& state, SSL* ssl,
                                         Ssl::HandshakerCallbacks& callbacks) {
  ASSERT(state != SocketState::HandshakeComplete && state != SocketState::ShutdownSent);
  int rc = SSL_do_handshake(ssl);
  if (rc == 1) {
    state = SocketState::HandshakeComplete;
    callbacks.LogHandshake(ssl);
    transport_socket_callbacks_->raiseEvent(Network::ConnectionEvent::Connected);

    // It's possible that we closed during the handshake callback.
    return transport_socket_callbacks_->connection().state() == Network::Connection::State::Open
               ? PostIoAction::KeepOpen
               : PostIoAction::Close;
  } else {
    int err = SSL_get_error(ssl, rc);
    switch (err) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      return PostIoAction::KeepOpen;
    case SSL_ERROR_WANT_PRIVATE_KEY_OPERATION:
      state = SocketState::HandshakeInProgress;
      return PostIoAction::KeepOpen;
    default:
      callbacks.ErrorCb();
      return PostIoAction::Close;
    }
  }
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
