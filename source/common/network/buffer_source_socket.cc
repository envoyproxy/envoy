#include "common/network/buffer_source_socket.h"

#include "envoy/network/transport_socket.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Network {

uint64_t BufferSourceSocket::next_bsid_ = 0;

BufferSourceSocket::BufferSourceSocket()
    : bsid_(next_bsid_++),
      read_buffer_(
          [this]() -> void {
            ENVOY_LOG_MISC(debug, "lambdai: BufferSourceTS {} is below high watermark", bsid());
            over_high_watermark_ = false;
          },
          [this]() -> void {
            ENVOY_LOG_MISC(debug, "lambdai: BufferSourceTS {} is over high watermark", bsid());
            over_high_watermark_ = true;
          },
          []() -> void {}) {
  ENVOY_LOG_MISC(debug, "lambdai: BufferSourceTS {} owns B{}", bsid(), read_buffer_.bid());
}

void BufferSourceSocket::setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) {
  ASSERT(!callbacks_);
  callbacks_ = &callbacks;
}

IoResult BufferSourceSocket::doRead(Buffer::Instance& buffer) {
  // Pipe impl: allow doRead any time before introducing error state.
  // So peer can only notify EOF(FIN) but not RST.

  // if (read_end_stream_ && read_buffer_.length() == 0) {
  //   ENVOY_CONN_LOG(trace, "read error: {}", callbacks_->connection(),
  //                  "no buffer to read from, closing.");
  //   return {PostIoAction::Close, 0, true};
  // }
  uint64_t bytes_read = 0;
  if (read_buffer_.length() > 0) {
    bytes_read = read_buffer_.length();
    buffer.move(read_buffer_);
  }
  ENVOY_CONN_LOG(trace, "read returns: {} read_end_stream_={}", callbacks_->connection(),
                 bytes_read, read_end_stream_);
  return {PostIoAction::KeepOpen, bytes_read, read_end_stream_};
}

IoResult BufferSourceSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  // TODO: check if the peer exist and want to write.
  lambdaiCheckSeeEndStream(end_stream);
  ASSERT(!shutdown_ || buffer.length() == 0);
  if (!isWritablePeerValid()) {
    ENVOY_CONN_LOG(debug, "lambdai: bs write error: {} {}", callbacks_->connection(),
                   buffer.length(), " bytes to write but no buffer to write to, closing. ");
    ENVOY_CONN_LOG(trace, "write error: {} {}", callbacks_->connection(), buffer.length(),
                   " bytes to write but no buffer to write to, closing. ");
    return {PostIoAction::Close, 0, false};
  }
  if (isWritablePeerOverHighWatermark()) {
    return {PostIoAction::KeepOpen, 0, false};
  }
  uint64_t bytes_written = 0;
  if (buffer.length() > 0) {
    bytes_written = buffer.length();
    writable_peer_->getWriteBuffer()->move(buffer);
  }
  // Since we move all bytes to the peer. doWrite always drain the buffer. It's safe to shutdown.
  // VS TCP: shutdown_ could be delayed if os buffer is full.
  if (end_stream) {
    // Debug only.
    shutdown_ = true;
    // Notify peer that no more data will be written. Think it sending the FIN.
    writable_peer_->setWriteEnd();
  }
  ENVOY_CONN_LOG(debug, "lambdai: bs write returns: {}", callbacks_->connection(), bytes_written);
  ENVOY_CONN_LOG(trace, "write returns: {}", callbacks_->connection(), bytes_written);
  return {PostIoAction::KeepOpen, bytes_written, false};
}

std::string BufferSourceSocket::protocol() const { return EMPTY_STRING; }
absl::string_view BufferSourceSocket::failureReason() const { return EMPTY_STRING; }

void BufferSourceSocket::onConnected() { callbacks_->raiseEvent(ConnectionEvent::Connected); }

TransportSocketPtr
BufferSourceSocketFactory::createTransportSocket(TransportSocketOptionsSharedPtr) const {
  return std::make_unique<BufferSourceSocket>();
}

bool BufferSourceSocketFactory::implementsSecureTransport() const { return false; }
} // namespace Network
} // namespace Envoy
