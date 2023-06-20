#pragma once

namespace Envoy {
namespace Ssl {

enum class SocketState { PreHandshake, HandshakeInProgress, HandshakeComplete, ShutdownSent };

} // namespace Ssl
} // namespace Envoy
