#pragma once

#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {

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

protected:
  Network::TransportSocketPtr transport_socket_;
};

} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy