#pragma once

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// Well-known filter state key for limiting TLS record sizes. When a network filter sets a
// UInt64Accessor with this key in the connection's filter state, the TLS transport socket
// uses its value as the maximum number of bytes written per SSL_write() call.
inline constexpr char TlsRecordSizeLimitKey[] = "envoy.transport_sockets.tls.record_size_limit";

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
