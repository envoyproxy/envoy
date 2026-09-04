#pragma once

#include <optional>

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "source/common/common/logger.h"
#include "source/common/network/transport_socket_options_impl.h"

namespace Envoy {
namespace Network {

class RawBufferSocket : public TransportSocket, protected Logger::Loggable<Logger::Id::connection> {
public:
  explicit RawBufferSocket(std::optional<uint64_t> max_read_buffer_size = std::nullopt)
      : max_read_buffer_size_(max_read_buffer_size) {}

  // Network::TransportSocket
  void setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  absl::string_view failureReason() const override;
  bool canFlushClose() override { return true; }
  void closeSocket(Network::ConnectionEvent, bool) override {}
  void onConnected() override;
  IoResult doRead(Buffer::Instance& buffer) override;
  IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override { return nullptr; }
  bool startSecureTransport() override { return false; }
  void configureInitialCongestionWindow(uint64_t, std::chrono::microseconds) override {}

protected:
  TransportSocketCallbacks* transportSocketCallbacks() const { return callbacks_; };

private:
  const std::optional<uint64_t> max_read_buffer_size_;
  // Filters may drain the shared connection buffer, so the read budget must remain monotonic.
  uint64_t accumulated_read_buffer_size_{};
  bool shutdown_{};
  TransportSocketCallbacks* callbacks_{};
};

class RawBufferSocketFactory : public DownstreamTransportSocketFactory,
                               public CommonUpstreamTransportSocketFactory {
public:
  explicit RawBufferSocketFactory(std::optional<uint64_t> max_read_buffer_size = std::nullopt)
      : max_read_buffer_size_(max_read_buffer_size) {}

  // Network::UpstreamTransportSocketFactory
  TransportSocketPtr createTransportSocket(TransportSocketOptionsConstSharedPtr,
                                           Upstream::HostDescriptionConstSharedPtr) const override;
  bool implementsSecureTransport() const override;
  absl::string_view defaultServerNameIndication() const override { return ""; }
  // Network::DownstreamTransportSocketFactory
  TransportSocketPtr createDownstreamTransportSocket() const override;

private:
  const std::optional<uint64_t> max_read_buffer_size_;
};

} // namespace Network
} // namespace Envoy
