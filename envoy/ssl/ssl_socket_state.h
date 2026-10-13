#pragma once

namespace Envoy {
namespace Ssl {

enum class SocketState {
  // The handshake is in progress and waiting on data on the connection, either to be sent or
  // received.
  HandshakeWaitingForConnectionData,

  // The handshake is in progress and waiting on Envoy to complete an operation before it can
  // continue.
  HandshakeBlockedOnAsyncOperation,

  // The handshake is complete.
  HandshakeComplete,

  // A shutdown signal has been sent on the connection.
  ShutdownSent
};

} // namespace Ssl
} // namespace Envoy
