#include "source/common/quic/quic_network_connection.h"

namespace Envoy {
namespace Quic {

QuicNetworkConnection::QuicNetworkConnection(Network::ConnectionSocketPtr&& connection_socket) {
  connection_sockets_.push_back(std::move(connection_socket));
}

QuicNetworkConnection::~QuicNetworkConnection() {
  for (auto& socket : connection_sockets_) {
    socket->close();
  }
}

uint64_t QuicNetworkConnection::id() const { return envoy_connection_->id(); }

} // namespace Quic
} // namespace Envoy
