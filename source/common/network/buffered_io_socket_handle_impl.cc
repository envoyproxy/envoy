#include "common/network/buffered_io_socket_handle_impl.h"

#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/event/file_event_impl.h"
#include "common/network/address_impl.h"

#include "absl/container/fixed_array.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Network {

BufferedIoSocketHandleImpl::BufferedIoSocketHandleImpl()
    : owned_buffer_(
          [this]() -> void {
            over_high_watermark_ = false;
            if (writable_peer_) {
              ENVOY_LOG(debug, "Socket {} switches to low watermark. Notify {}.",
                        static_cast<void*>(this), static_cast<void*>(writable_peer_));
              writable_peer_->onPeerBufferWritable();
            }
          },
          [this]() -> void {
            over_high_watermark_ = true;
            // Low to high is checked by peer after peer writes data.
          },
          []() -> void {}) {}

Api::IoCallUint64Result BufferedIoSocketHandleImpl::close() {
  ASSERT(!closed_);
  if (!write_shutdown_) {
    ASSERT(writable_peer_);
    // Notify the peer we won't write more data. shutdown(WRITE).
    writable_peer_->setWriteEnd();
    // Notify the peer that we no longer accept data. shutdown(RD).
    writable_peer_->onPeerDestroy();
    writable_peer_->maybeSetNewData();
    writable_peer_ = nullptr;
    write_shutdown_ = true;
  }
  closed_ = true;
  return Api::ioCallUint64ResultNoError();
}

bool BufferedIoSocketHandleImpl::isOpen() const { return !closed_; }

Api::IoCallUint64Result BufferedIoSocketHandleImpl::readv(uint64_t max_length,
                                                          Buffer::RawSlice* slices,
                                                          uint64_t num_slice) {
  if (!isOpen()) {
    return {0,
            Api::IoErrorPtr(new IoSocketError(SOCKET_ERROR_INVAL), IoSocketError::deleteIoError)};
  }
  if (owned_buffer_.length() == 0) {
    if (read_end_stream_) {
      return {0, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})};
    } else {
      return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                                 IoSocketError::deleteIoError)};
    }
  } else {
    absl::FixedArray<iovec> iov(num_slice);
    uint64_t num_slices_to_read = 0;
    uint64_t bytes_to_read = 0;
    for (; num_slices_to_read < num_slice && bytes_to_read < max_length; num_slices_to_read++) {
      auto max_bytes_to_read =
          std::min(std::min(owned_buffer_.length(), max_length) - bytes_to_read,
                   uint64_t(slices[num_slices_to_read].len_));
      owned_buffer_.copyOut(bytes_to_read, max_bytes_to_read, slices[num_slices_to_read].mem_);
      bytes_to_read += max_bytes_to_read;
    }
    ASSERT(bytes_to_read <= max_length);
    owned_buffer_.drain(bytes_to_read);
    ENVOY_LOG(trace, "socket {} readv {} bytes", static_cast<void*>(this), bytes_to_read);
    return {bytes_to_read, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})};
  }
}

Api::IoCallUint64Result BufferedIoSocketHandleImpl::read(Buffer::Instance& buffer,
                                                         uint64_t max_length) {
  if (!isOpen()) {
    return {0,
            Api::IoErrorPtr(new IoSocketError(SOCKET_ERROR_INVAL), IoSocketError::deleteIoError)};
  }
  if (owned_buffer_.length() == 0) {
    if (read_end_stream_) {
      return {0, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})};
    } else {
      return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                                 IoSocketError::deleteIoError)};
    }
  } else {
    // TODO(lambdai): Move at slice boundary to move to reduce the copy.
    uint64_t max_bytes_to_read = std::min(max_length, owned_buffer_.length());
    buffer.move(owned_buffer_, max_bytes_to_read);
    return {max_bytes_to_read, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})};
  }
}

Api::IoCallUint64Result BufferedIoSocketHandleImpl::writev(const Buffer::RawSlice* slices,
                                                           uint64_t num_slice) {
  if (!isOpen()) {
    return {0,
            Api::IoErrorPtr(new IoSocketError(SOCKET_ERROR_INVAL), IoSocketError::deleteIoError)};
  }
  if (!writable_peer_) {
    return sysCallResultToIoCallResult(Api::SysCallSizeResult{-1, SOCKET_ERROR_INVAL});
  }
  if (writable_peer_->isWriteEndSet() || !writable_peer_->isWritable()) {
    return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                               IoSocketError::deleteIoError)};
  }
  // Write along with iteration. Buffer guarantee the fragment is always append-able.
  uint64_t total_bytes_to_write = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      writable_peer_->getWriteBuffer()->add(slices[i].mem_, slices[i].len_);
      total_bytes_to_write += slices[i].len_;
    }
  }
  writable_peer_->maybeSetNewData();
  ENVOY_LOG(trace, "socket {} writev {} bytes", static_cast<void*>(this), total_bytes_to_write);
  return {total_bytes_to_write, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})};
}

Api::IoCallUint64Result BufferedIoSocketHandleImpl::write(Buffer::Instance& buffer) {
  if (!isOpen()) {
    return {0,
            Api::IoErrorPtr(new IoSocketError(SOCKET_ERROR_INVAL), IoSocketError::deleteIoError)};
  }
  if (!writable_peer_) {
    return {0,
            Api::IoErrorPtr(new IoSocketError(SOCKET_ERROR_INVAL), IoSocketError::deleteIoError)};
  }
  if (writable_peer_->isWriteEndSet() || !writable_peer_->isWritable()) {
    return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                               IoSocketError::deleteIoError)};
  }
  uint64_t total_bytes_to_write = buffer.length();
  writable_peer_->getWriteBuffer()->move(buffer);
  writable_peer_->maybeSetNewData();
  ENVOY_LOG(trace, "socket {} writev {} bytes", static_cast<void*>(this), total_bytes_to_write);
  return {total_bytes_to_write, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})};
}

Api::IoCallUint64Result BufferedIoSocketHandleImpl::sendmsg(const Buffer::RawSlice*, uint64_t, int,
                                                            const Address::Ip*,
                                                            const Address::Instance&) {
  return IoSocketError::ioResultSocketInvalidAddress();
}

Api::IoCallUint64Result BufferedIoSocketHandleImpl::recvmsg(Buffer::RawSlice*, const uint64_t,
                                                            uint32_t, RecvMsgOutput&) {
  return IoSocketError::ioResultSocketInvalidAddress();
}

Api::IoCallUint64Result BufferedIoSocketHandleImpl::recvmmsg(RawSliceArrays&, uint32_t,
                                                             RecvMsgOutput&) {
  return IoSocketError::ioResultSocketInvalidAddress();
}

Api::IoCallUint64Result BufferedIoSocketHandleImpl::recv(void* buffer, size_t length, int flags) {
  if (!isOpen()) {
    return {0,
            Api::IoErrorPtr(new IoSocketError(SOCKET_ERROR_INVAL), IoSocketError::deleteIoError)};
  }
  // No data and the writer closed.
  if (owned_buffer_.length() == 0) {
    if (read_end_stream_) {
      return {0, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})};
    } else {
      return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                                 IoSocketError::deleteIoError)};
    }
  } else {
    auto max_bytes_to_read = std::min(owned_buffer_.length(), length);
    owned_buffer_.copyOut(0, max_bytes_to_read, buffer);
    if (!(flags & MSG_PEEK)) {
      owned_buffer_.drain(max_bytes_to_read);
    }
    return {max_bytes_to_read, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})};
  }
}

bool BufferedIoSocketHandleImpl::supportsMmsg() const { return false; }

bool BufferedIoSocketHandleImpl::supportsUdpGro() const { return false; }

Api::SysCallIntResult makeInvalidSyscall() {
  return Api::SysCallIntResult{-1, SOCKET_ERROR_NOT_SUP /*SOCKET_ERROR_NOT_SUP*/};
}

Api::SysCallIntResult BufferedIoSocketHandleImpl::bind(Address::InstanceConstSharedPtr) {
  return makeInvalidSyscall();
}

Api::SysCallIntResult BufferedIoSocketHandleImpl::listen(int) { return makeInvalidSyscall(); }

IoHandlePtr BufferedIoSocketHandleImpl::accept(struct sockaddr*, socklen_t*) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

Api::SysCallIntResult BufferedIoSocketHandleImpl::connect(Address::InstanceConstSharedPtr) {
  // Buffered Io handle should always be considered as connected. Use write to determine if peer is
  // closed.
  return {0, 0};
}

Api::SysCallIntResult BufferedIoSocketHandleImpl::setOption(int, int, const void*, socklen_t) {
  return makeInvalidSyscall();
}

Api::SysCallIntResult BufferedIoSocketHandleImpl::getOption(int, int, void*, socklen_t*) {
  return makeInvalidSyscall();
}

Api::SysCallIntResult BufferedIoSocketHandleImpl::setBlocking(bool) { return makeInvalidSyscall(); }

absl::optional<int> BufferedIoSocketHandleImpl::domain() { return absl::nullopt; }

Address::InstanceConstSharedPtr BufferedIoSocketHandleImpl::localAddress() {
  throw EnvoyException(fmt::format("getsockname failed for BufferedIoSocketHandleImpl"));
}

Address::InstanceConstSharedPtr BufferedIoSocketHandleImpl::peerAddress() {

  throw EnvoyException(fmt::format("getsockname failed for BufferedIoSocketHandleImpl"));
}

Event::FileEventPtr BufferedIoSocketHandleImpl::createFileEvent(Event::Dispatcher& dispatcher,
                                                                Event::FileReadyCb cb,
                                                                Event::FileTriggerType trigger_type,
                                                                uint32_t events) {
  ASSERT(event_counter_ == 0);
  ++event_counter_;
  io_callback_ = dispatcher.createSchedulableCallback([this]() { user_file_event_->onEvents(); });
  auto res = Event::UserSpaceFileEventFactory::createUserSpaceFileEventImpl(
      dispatcher, cb, trigger_type, events, *io_callback_, event_counter_);
  user_file_event_ = res.get();
  // Blindly activate the events.
  io_callback_->scheduleCallbackNextIteration();
  return res;
}

Api::SysCallIntResult BufferedIoSocketHandleImpl::shutdown(int how) {
  // Support shutdown write.
  if ((how == ENVOY_SHUT_WR) || (how == ENVOY_SHUT_RDWR)) {
    ASSERT(!closed_);
    if (!write_shutdown_) {
      ASSERT(writable_peer_);
      // Notify the peer we won't write more data. shutdown(WRITE).
      writable_peer_->setWriteEnd();
      writable_peer_->maybeSetNewData();
      write_shutdown_ = true;
    }
  }
  return {0, 0};
}

} // namespace Network
} // namespace Envoy