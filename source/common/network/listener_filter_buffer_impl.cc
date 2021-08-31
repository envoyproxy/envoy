#include "source/common/network/listener_filter_buffer_impl.h"

#include <string.h>

namespace Envoy {
namespace Network {

uint64_t ListenerFilterBufferImpl::copyOut(void* buffer, uint64_t length) {
  auto size = std::min(length, data_size_);
  if (data_size_ == 0) {
    return 0;
  }
  memcpy(buffer, base_, size); // NOLINT(safe-memcpy)
  return size;
}

uint64_t ListenerFilterBufferImpl::drain(uint64_t length) {
  auto size_to_drain = std::min(length, data_size_);
  base_ += size_to_drain;
  data_size_ -= size_to_drain;
  return size_to_drain;
}

bool ListenerFilterBufferImpl::drainFromSocket() {
  std::unique_ptr<uint8_t[]> buf(new uint8_t[drainedSize()]);
  auto result = io_handle_.recv(buf.get(), drainedSize(), 0);
  if (!result.ok()) {
    on_close_cb_();
    return false;
  }
  drained_to_socket = true;
  return true;
}

void ListenerFilterBufferImpl::onFileEvent(uint32_t events) {
  if (events & Event::FileReadyType::Closed) {
    on_close_cb_();
  }

  const auto result = io_handle_.recv(buffer_.get(), buffer_size_, MSG_PEEK);
  if (!result.ok()) {
    if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
      return;
    }
    on_close_cb_();
    return;
  }

  // the drain already sync to socket, so reset the base_ to the beginning of the buffer.
  if (drained_to_socket) {
    base_ = buffer_.get();
    drained_to_socket = false;
  }

  // the data may be drained.
  data_size_ = result.return_value_ - drainedSize();
  on_data_cb_();
}

} // namespace Network
} // namespace Envoy