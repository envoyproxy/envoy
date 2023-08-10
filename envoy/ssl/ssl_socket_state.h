#pragma once

namespace Envoy {
namespace Ssl {

enum class SocketState {
  PreHandshake,
  HandshakeInProgressByPrivateKeyOperation,
  HandshakeInProgressByCertificateVerification,
  HandshakeComplete,
  ShutdownSent
};

} // namespace Ssl
} // namespace Envoy
