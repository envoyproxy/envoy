#include "source/extensions/transport_sockets/common/passthrough.h"

#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {

PassthroughSocket::PassthroughSocket(Network::TransportSocketPtr&& transport_socket)
    : transport_socket_(std::move(transport_socket)) {}

void PassthroughSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  transport_socket_->setTransportSocketCallbacks(callbacks);
}

std::string PassthroughSocket::protocol() const { return transport_socket_->protocol(); }

absl::string_view PassthroughSocket::failureReason() const {
  return transport_socket_->failureReason();
}

bool PassthroughSocket::canFlushClose() { return transport_socket_->canFlushClose(); }

Api::SysCallIntResult PassthroughSocket::connect(Network::ConnectionSocket& socket) {
  return transport_socket_->connect(socket);
}

void PassthroughSocket::closeSocket(Network::ConnectionEvent event) {
  transport_socket_->closeSocket(event);
}

Network::IoResult PassthroughSocket::doRead(Buffer::Instance& buffer) {
  return transport_socket_->doRead(buffer);
}

Network::IoResult PassthroughSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  return transport_socket_->doWrite(buffer, end_stream);
}

void PassthroughSocket::onConnected() { transport_socket_->onConnected(); }

Ssl::ConnectionInfoConstSharedPtr PassthroughSocket::ssl() const {
  return transport_socket_->ssl();
}

void PassthroughSocket::configureInitialCongestionWindow(uint64_t bandwidth_bits_per_sec,
                                                         std::chrono::microseconds rtt) {
  return transport_socket_->configureInitialCongestionWindow(bandwidth_bits_per_sec, rtt);
}

} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
