#include "common/quic/envoy_quic_connection.h"

namespace Envoy {
namespace Quic {

EnvoyQuicConnection::EnvoyQuicConnection(Network::ConnectionSocketPtr&& connection_socket)
    : connection_socket_(std::move(connection_socket)) {}

EnvoyQuicConnection::~EnvoyQuicConnection() { connection_socket_->close(); }

uint64_t EnvoyQuicConnection::id() const { return envoy_connection_->id(); }

} // namespace Quic
} // namespace Envoy
