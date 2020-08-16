#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Network {

class BufferSourceSocket : public TransportSocket,
                           public WritablePeer,
                           protected Logger::Loggable<Logger::Id::connection> {
public:
  uint64_t bsid() { return bsid_; }

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

  Buffer::WatermarkBuffer& getTransportSocketBuffer() { return read_buffer_; }

  uint64_t bsid_;
  Buffer::WatermarkBuffer read_buffer_;
  // True if read_buffer_ is not addable. Note that read_buffer_ may have pending data to drain.
  bool read_end_stream_{false};

  // WritablePeer
  void setWriteEnd() override {
    ASSERT(!read_end_stream_);
    read_end_stream_ = true;
    ENVOY_LOG_MISC(debug, "lambdai: B{} set write end = true", bsid());
  }
  Buffer::Instance* getWriteBuffer() override { return &read_buffer_; }

  void setWritablePeer(WritablePeer* writable_peer) {
    // Swapping writable peer is undefined behavior.
    ASSERT(!writable_peer_);
    ASSERT(!peer_closed_);

    ENVOY_LOG_MISC(debug, "lambdai: C{} set write dst buffer to B{}", callbacks_->connection().id(),
                   writable_peer->getWriteBuffer()->bid());
    writable_peer_ = writable_peer;
  }
  void clearWritablePeer() {
    ASSERT(writable_peer_);
    writable_peer_ = nullptr;
    ASSERT(!peer_closed_);
    peer_closed_ = true;
  }
  bool isWritablePeerValid() const { return !peer_closed_; }

private:
  static uint64_t next_bsid_;
  TransportSocketCallbacks* callbacks_{};
  bool shutdown_{};
  // Buffer::WatermarkBuffer read_buffer_;

  WritablePeer* writable_peer_{nullptr};
  // The flag whether the peer is valid. Any write attempt should check flag.
  bool peer_closed_{false};
};

class BufferSourceSocketFactory : public TransportSocketFactory {
public:
  // Network::TransportSocketFactory
  TransportSocketPtr createTransportSocket(TransportSocketOptionsSharedPtr options) const override;
  bool implementsSecureTransport() const override;
};

} // namespace Network
} // namespace Envoy
