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

PostIoAction HandshakerImpl::doHandshake(SocketState& state, Ssl::HandshakerCallbacks& callbacks) {
  ASSERT(state != SocketState::HandshakeComplete && state != SocketState::ShutdownSent);
  int rc = SSL_do_handshake(ssl_.get());
  if (rc == 1) {
    state = SocketState::HandshakeComplete;
    callbacks.OnSuccessCb(ssl_.get());

    // It's possible that we closed during the handshake callback.
    return transport_socket_callbacks_->connection().state() == Network::Connection::State::Open
               ? PostIoAction::KeepOpen
               : PostIoAction::Close;
  } else {
    switch (SSL_get_error(ssl_.get(), rc)) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      return PostIoAction::KeepOpen;
    case SSL_ERROR_WANT_PRIVATE_KEY_OPERATION:
      state = SocketState::HandshakeInProgress;
      return PostIoAction::KeepOpen;
    default:
      callbacks.OnFailureCb();
      return PostIoAction::Close;
    }
  }
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
