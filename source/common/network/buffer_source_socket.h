#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Network {

class BufferSourceSocket : public TransportSocket,
                           protected Logger::Loggable<Logger::Id::connection> {
public:
  BufferSourceSocket();
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

  void setReadSourceBuffer(Buffer::Instance* read_source_buf) {
    ENVOY_LOG_MISC(debug, "lambdai: SHOULD NOT REACH C{} set read source buffer to {}",
                   callbacks_->connection().id(), static_cast<void*>(read_source_buf));
    // read_source_buf_ = read_source_buf;
  }
  void setWriteDestBuffer(Buffer::Instance* write_dest_buf) {
    ENVOY_LOG_MISC(debug, "lambdai: C{} set write dst buffer to {}", callbacks_->connection().id(),
                   static_cast<void*>(write_dest_buf));
    write_dest_buf_ = write_dest_buf;
  }

  Buffer::WatermarkBuffer& getTransportSocketBuffer() { return read_buffer_; }

private:
  TransportSocketCallbacks* callbacks_{};
  bool shutdown_{};
  Buffer::WatermarkBuffer read_buffer_;
  bool read_end_stream_{false};
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
