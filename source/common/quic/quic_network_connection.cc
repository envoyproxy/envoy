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

void QuicNetworkConnection::setEnvoyConnection(Network::Connection& connection,
                                               QuicWriteEventCallback& write_callback) {
  envoy_connection_ = &connection;
  write_callback_ = &write_callback;
}

void QuicNetworkConnection::onWriteEventDone() {
  if (write_callback_ != nullptr) {
    write_callback_->onWriteEventDone();
  }
}

} // namespace Quic
} // namespace Envoy
