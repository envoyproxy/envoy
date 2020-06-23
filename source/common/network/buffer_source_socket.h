#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Network {

class BufferSourceSocket : public TransportSocket,
                           protected Logger::Loggable<Logger::Id::connection> {
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

private:
  TransportSocketCallbacks* callbacks_{};
  bool shutdown_{};
  Buffer::Instance* read_source_buf_;
  Buffer::Instance* write_dest_buf_;
};

class BufferSourceSocketFactory : public TransportSocketFactory {
public:
  // Network::TransportSocketFactory
  TransportSocketPtr createTransportSocket(TransportSocketOptionsSharedPtr options) const override;
  bool implementsSecureTransport() const override;
};

} // namespace Network
} // namespace Envoy
