#include "common/network/buffer_source_socket.h"

#include "envoy/network/transport_socket.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Network {
BufferSourceSocket::BufferSourceSocket()
    : read_buffer_([]() -> void {}, []() -> void {}, []() -> void {}) {}
void BufferSourceSocket::setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) {
  ASSERT(!callbacks_);
  callbacks_ = &callbacks;
}

IoResult BufferSourceSocket::doRead(Buffer::Instance& buffer) {
  if (read_end_stream_ && read_buffer_.length() == 0) {
    ENVOY_CONN_LOG(trace, "read error: {}", callbacks_->connection(),
                   "no buffer to read from, closing.");
    return {PostIoAction::Close, 0, true};
  }
  uint64_t bytes_read = 0;
  if (read_buffer_.length() > 0) {
    bytes_read = read_buffer_.length();
    buffer.move(read_buffer_);
  }
  ENVOY_CONN_LOG(trace, "read returns: {}", callbacks_->connection(), bytes_read);
  return {PostIoAction::KeepOpen, bytes_read, false};
}

IoResult BufferSourceSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  ASSERT(!shutdown_ || buffer.length() == 0);
  if (write_dest_buf_ == nullptr) {
    ENVOY_CONN_LOG(trace, "write error: {} {}", callbacks_->connection(), buffer.length(),
                   " bytes to write but no buffer to write to, closing. ");
    return {PostIoAction::Close, 0, false};
  }
  uint64_t bytes_written = 0;
  if (buffer.length() > 0) {
    bytes_written = buffer.length();
    write_dest_buf_->move(buffer);
  }
  ENVOY_CONN_LOG(trace, "write returns: {}", callbacks_->connection(), bytes_written);
  return {buffer.length() == 0 && end_stream ? PostIoAction::Close : PostIoAction::KeepOpen,
          bytes_written, false};
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
