#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Network {

class RawBufferSocket : public TransportSocket, protected Logger::Loggable<Logger::Id::connection> {
public:
  // Network::TransportSocket
  void setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  absl::string_view failureReason() const override;
  bool canFlushClose() override { return true; }
  void closeSocket(Network::ConnectionEvent) override {}
  void onConnected() override;
  IoResult doRead(Buffer::Instance& buffer) override;
  IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override { return nullptr; }
  bool startSecureTransport() override { return false; }

private:
  TransportSocketCallbacks* callbacks_{};
  bool shutdown_{};
};

class RawBufferSocketFactory : public TransportSocketFactory {
public:
  // Network::TransportSocketFactory
  TransportSocketPtr
  createTransportSocket(TransportSocketOptionsConstSharedPtr options) const override;
  bool implementsSecureTransport() const override;
  bool usesProxyProtocolOptions() const override { return false; }
};

} // namespace Network
} // namespace Envoy
