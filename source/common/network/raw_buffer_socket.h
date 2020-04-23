#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Network {

class RawBufferSocket : public TransportSocket, protected Logger::Loggable<Logger::Id::connection> {
public:
  explicit RawBufferSocket(TransportSocketOptionsSharedPtr options) : options_(std::move(options)) {}

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
  TransportSocketOptionsSharedPtr options() const override { return options_; }

private:
  // This implementation doesn't actually use the options, but surfacing the provided options here
  // helps code validating what options were passed to the factory.
  // TODO(snowp): Not great to extend the API because of test, figure out something better.
  const TransportSocketOptionsSharedPtr options_;
  TransportSocketCallbacks* callbacks_{};
  bool shutdown_{};
};

class RawBufferSocketFactory : public TransportSocketFactory {
public:
  // Network::TransportSocketFactory
  TransportSocketPtr createTransportSocket(TransportSocketOptionsSharedPtr options) const override;
  bool implementsSecureTransport() const override;
};

} // namespace Network
} // namespace Envoy
