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
                           public ReadableSource,
                           protected Logger::Loggable<Logger::Id::connection> {
public:
  uint64_t bsid() { return bsid_; }

  BufferSourceSocket();

  void setEventSchedulable(Network::EventSchedulable* schedulable) {
    ASSERT(schedulable_ == nullptr);
    schedulable_ = schedulable;
  }
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
  // Drained to empty. The following write should switch the state to false.
  bool last_read_to_empty_{true};
  // True if read_buffer_ is not addable. Note that read_buffer_ may have pending data to drain.
  bool read_end_stream_{false};

  // ReadableSource
  bool isPeerShutDownWrite() const override { return read_end_stream_; }
  bool isReadable() const override { return isPeerShutDownWrite() || read_buffer_.length() > 0; }
  // WritablePeer
  void setWriteEnd() override {
    // TODO(lambdai): Avoid sending duplicated EOS.
    // ASSERT(!read_end_stream_);
    read_end_stream_ = true;
    ENVOY_LOG_MISC(debug, "lambdai: B{} set write end = true", bsid());
  }

  void maybeSetNewData() override {
    if (last_read_to_empty_) {
      last_read_to_empty_ = false;
      schedulable_->scheduleWriteEvent();
      schedulable_->scheduleNextEvent();
    }
  }

  Buffer::Instance* getWriteBuffer() override { return &read_buffer_; }

  bool isOverHighWatermark() const override { return over_high_watermark_; }

  bool triggeredHighToLowWatermark() const override { return triggered_high_to_low_watermark_; }
  void clearTriggeredHighToLowWatermark() override { triggered_high_to_low_watermark_ = false; }
  void setTriggeredHighToLowWatermark() override { triggered_high_to_low_watermark_ = true; }

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
  bool isWritablePeerOverHighWatermark() const { return writable_peer_->isOverHighWatermark(); }

private:
  static uint64_t next_bsid_;
  TransportSocketCallbacks* callbacks_{};
  bool shutdown_{};
  // Buffer::WatermarkBuffer read_buffer_;

  WritablePeer* writable_peer_{nullptr};
  // The flag whether the peer is valid. Any write attempt should check flag.
  bool peer_closed_{false};
  bool over_high_watermark_{false};
  bool triggered_high_to_low_watermark_{true};
  EventSchedulable* schedulable_{nullptr};
};

class BufferSourceSocketFactory : public TransportSocketFactory {
public:
  // Network::TransportSocketFactory
  TransportSocketPtr createTransportSocket(TransportSocketOptionsSharedPtr options) const override;
  bool implementsSecureTransport() const override;
};

} // namespace Network
} // namespace Envoy
