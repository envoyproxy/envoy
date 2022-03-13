#include "source/common/network/listener_filter_buffer_impl.h"

#include <string>

namespace Envoy {
namespace Network {

ListenerFilterBufferImpl::ListenerFilterBufferImpl(IoHandle& io_handle,
                                                   Event::Dispatcher& dispatcher,
                                                   ListenerFilterBufferOnCloseCb close_cb,
                                                   ListenerFilterBufferOnDataCb on_data_cb,
                                                   uint64_t buffer_size)
    : io_handle_(io_handle), dispatcher_(dispatcher), on_close_cb_(close_cb),
      on_data_cb_(on_data_cb), buffer_(std::make_unique<uint8_t[]>(buffer_size)),
      base_(buffer_.get()), buffer_size_(buffer_size) {
  // If the buffer_size not greater than 0, it means that doesn't expect any data.
  ASSERT(buffer_size > 0);

  io_handle_.initializeFileEvent(
      dispatcher_, [this](uint32_t events) { onFileEvent(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
}

const Buffer::ConstRawSlice ListenerFilterBufferImpl::rawSlice() const {
  Buffer::ConstRawSlice slice;
  slice.mem_ = base_;
  slice.len_ = data_size_;
  return slice;
}

bool ListenerFilterBufferImpl::drain(uint64_t length) {
  if (length == 0) {
    return true;
  }

  ASSERT(length <= data_size_);

  uint64_t read_size = 0;
  while (read_size < length) {
    auto result = io_handle_.recv(base_, length - read_size, 0);
    ENVOY_LOG(trace, "recv returned: {}", result.return_value_);

    if (!result.ok()) {
      // `IoErrorCode::Again` isn't proccessed here, since
      // the data already in the socket buffer.
      return false;
    }
    read_size += result.return_value_;
  }
  base_ += length;
  data_size_ -= length;
  buffer_size_ -= length;
  return true;
}

PeekState ListenerFilterBufferImpl::peekFromSocket() {
  const auto result = io_handle_.recv(base_, buffer_size_, MSG_PEEK);
  ENVOY_LOG(trace, "recv returned: {}", result.return_value_);

  if (!result.ok()) {
    if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
      ENVOY_LOG(trace, "recv return try again");
      return PeekState::Again;
    }
    ENVOY_LOG(debug, "recv failed: {}: {}", result.err_->getErrorCode(),
              result.err_->getErrorDetails());
    return PeekState::Error;
  }
  // Remote closed
  if (result.return_value_ == 0) {
    ENVOY_LOG(debug, "recv failed: remote closed");
    return PeekState::RemoteClose;
  }
  data_size_ = result.return_value_;
  ASSERT(data_size_ <= buffer_size_);

  return PeekState::Done;
}

void ListenerFilterBufferImpl::activateFileEvent(uint32_t events) { onFileEvent(events); }

void ListenerFilterBufferImpl::onFileEvent(uint32_t events) {
  ENVOY_LOG(trace, "onFileEvent: {}", events);

  auto state = peekFromSocket();
  if (state == PeekState::Done) {
    on_data_cb_(*this);
  } else if (state == PeekState::Error) {
    on_close_cb_(true);
  } else if (state == PeekState::RemoteClose) {
    on_close_cb_(false);
  }
  // Did nothing for `Api::IoError::IoErrorCode::Again`
}

} // namespace Network
} // namespace Envoy
