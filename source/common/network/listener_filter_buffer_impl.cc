#include "source/common/network/listener_filter_buffer_impl.h"

#include <string.h>

namespace Envoy {
namespace Network {

const Buffer::ConstRawSlice ListenerFilterBufferImpl::rawSlice() const {
  Buffer::ConstRawSlice slice;
  auto front_slice = buffer_->frontSlice();
  slice.mem_ = static_cast<uint8_t*>(front_slice.mem_) + drained_size_;
  slice.len_ = data_size_;
  return slice;
}

uint64_t ListenerFilterBufferImpl::drain(uint64_t length) {
  auto size_to_drain = std::min(length, data_size_);
  // It doesn't drain the data from buffer directly until drain the data
  // from actual socket.
  drained_size_ += size_to_drain;
  data_size_ -= size_to_drain;
  return size_to_drain;
}

bool ListenerFilterBufferImpl::drainFromSocket() {
  // Since we want to drain the data from the socket, so a
  // temporary buffer need here.
  std::unique_ptr<uint8_t[]> buf(new uint8_t[drained_size_]);
  auto result = io_handle_.recv(buf.get(), drained_size_, 0);
  if (!result.ok()) {
    on_close_cb_();
    return false;
  }
  buffer_->drain(drained_size_);
  drained_size_ = 0;
  return true;
}

PeekState ListenerFilterBufferImpl::peekFromSocket() {
  auto raw_slice = buffer_->frontSlice();

  const auto result = io_handle_.recv(raw_slice.mem_, raw_slice.len_, MSG_PEEK);
  if (!result.ok()) {
    if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
      return PeekState::Again;
    }
    return PeekState::Error;
  }
  data_size_ = result.return_value_ - drained_size_;
  return PeekState::Done;
}

void ListenerFilterBufferImpl::onFileEvent(uint32_t events) {
  if (events & Event::FileReadyType::Closed) {
    on_close_cb_();
  }

  auto state = peekFromSocket();
  if (state == PeekState::Done) {
    on_data_cb_();
  } else if (state == PeekState::Error) {
    on_close_cb_();
  }
}

} // namespace Network
} // namespace Envoy