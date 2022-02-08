#pragma once

#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {

class PassthroughFactory : public Network::TransportSocketFactory {
public:
  PassthroughFactory(Network::TransportSocketFactoryPtr&& transport_socket_factory)
      : transport_socket_factory_(std::move(transport_socket_factory)) {
    ASSERT(transport_socket_factory_ != nullptr);
  }

  bool implementsSecureTransport() const override {
    return transport_socket_factory_->implementsSecureTransport();
  }
  bool supportsAlpn() const override { return transport_socket_factory_->supportsAlpn(); }
  void hashKey(std::vector<uint8_t>& key,
               Network::TransportSocketOptionsConstSharedPtr options) const override;

protected:
  // The wrapped factory.
  Network::TransportSocketFactoryPtr transport_socket_factory_;
};

class PassthroughSocket : public Network::TransportSocket {
public:
  PassthroughSocket(Network::TransportSocketPtr&& transport_socket);

  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  absl::string_view failureReason() const override;
  bool canFlushClose() override;
  void closeSocket(Network::ConnectionEvent event) override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  void onConnected() override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override;
  // startSecureTransport method should not be called for this transport socket.
  bool startSecureTransport() override { return false; }

protected:
  Network::TransportSocketPtr transport_socket_;
};

} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
