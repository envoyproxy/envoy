#include "source/common/quic/quic_network_connection.h"

namespace Envoy {
namespace Quic {

QuicNetworkConnection::QuicNetworkConnection(Network::ConnectionSocketPtr&& connection_socket)
    : connection_socket_(std::move(connection_socket)) {}

QuicNetworkConnection::~QuicNetworkConnection() { connection_socket_->close(); }

uint64_t QuicNetworkConnection::id() const { return envoy_connection_->id(); }

} // namespace Quic
} // namespace Envoy
