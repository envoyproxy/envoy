#include "source/common/network/listener_filter_buffer_impl.h"

#include <bits/stdint-uintn.h>
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
  ASSERT(length <= data_size_);
  auto size_to_drain = std::min(length, data_size_);
  // It doesn't drain the data from buffer directly until drain the data
  // from actual socket.
  drained_size_ += size_to_drain;
  data_size_ -= size_to_drain;
  return size_to_drain;
}

bool ListenerFilterBufferImpl::drainFromSocket() {
  if (drained_size_ == 0) {
    return true;
  }
  // Since we want to drain the data from the socket, so a
  // temporary buffer need here.
  std::unique_ptr<uint8_t[]> buf(new uint8_t[drained_size_]);
  uint64_t read_size = 0;
  while (1) {
    auto result = io_handle_.recv(buf.get(), drained_size_ - read_size, 0);
    ENVOY_LOG(trace, "recv returned: {}", result.return_value_);

    if (!result.ok()) {
      if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
        continue;
      }
      ENVOY_LOG(debug, "recv failed: {}: {}", result.err_->getErrorCode(),
                result.err_->getErrorDetails());
      return false;
    }
    // remote closed
    if (result.return_value_ == 0) {
      ENVOY_LOG(debug, "recv failed: remote closed");
      return false;
    }
    read_size += result.return_value_;
    if (read_size < drained_size_) {
      continue;
    }
    buffer_->drain(drained_size_);
    drained_size_ = 0;
    break;
  }
  return true;
}

PeekState ListenerFilterBufferImpl::peekFromSocket() {
  // For getting continuous memory space, we allocate single
  // slice in the constructor. so we can assume there is only
  // one slice in the buffer.
  ASSERT(buffer_->getRawSlices().size() == 1);
  auto raw_slice = buffer_->frontSlice();

  const auto result = io_handle_.recv(raw_slice.mem_, raw_slice.len_, MSG_PEEK);
  ENVOY_LOG(trace, "recv returned: {}", result.return_value_);

  if (!result.ok()) {
    if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
      return PeekState::Again;
    }
    ENVOY_LOG(debug, "recv failed: {}: {}", result.err_->getErrorCode(),
              result.err_->getErrorDetails());
    return PeekState::Error;
  }
  // remote closed
  if (result.return_value_ == 0) {
    ENVOY_LOG(debug, "recv failed: remote closed");
    return PeekState::Error;
  }
  data_size_ = result.return_value_ - drained_size_;
  return PeekState::Done;
}

void ListenerFilterBufferImpl::onFileEvent(uint32_t events) {
  if (events & Event::FileReadyType::Closed) {
    on_close_cb_();
    return;
  }

  auto state = peekFromSocket();
  if (state == PeekState::Done) {
    on_data_cb_();
  } else if (state == PeekState::Error) {
    on_close_cb_();
  }
  // did nothing for `Api::IoError::IoErrorCode::Again`
}

} // namespace Network
} // namespace Envoy