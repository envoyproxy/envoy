#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Network {

class RawBufferSocket : public TransportSocket, protected Logger::Loggable<Logger::Id::connection> {
public:
  // Network::TransportSocket
  void setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  bool canFlushClose() override { return true; }
  void closeSocket(Network::ConnectionEvent) override {}
  void onConnected() override;
  IoResult doRead(Buffer::Instance& buffer) override;
  IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  Ssl::Connection* ssl() override { return nullptr; }
  const Ssl::Connection* ssl() const override { return nullptr; }

private:
  TransportSocketCallbacks* callbacks_{};
  bool shutdown_{};
};

class RawBufferSocketFactory : public TransportSocketFactory {
public:
  // Network::TransportSocketFactory
  TransportSocketPtr createTransportSocket() const override;
  bool implementsSecureTransport() const override;
};

} // namespace Network
} // namespace Envoy
