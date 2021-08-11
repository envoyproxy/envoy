#include "source/extensions/io_socket/user_space/io_handle_impl.h"

#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/io_socket/user_space/file_event_impl.h"

#include "absl/types/optional.h"

namespace Envoy {

namespace Extensions {
namespace IoSocket {
namespace UserSpace {
namespace {
Api::SysCallIntResult makeInvalidSyscallResult() {
  return Api::SysCallIntResult{-1, SOCKET_ERROR_NOT_SUP};
}

/**
 * Move at most max_length from src to dst. If the dst is close or beyond high watermark, move no
 * more than 16K. It's not an error if src buffer doesn't contain enough data.
 * @param dst supplies the buffer where the data is move to.
 * @param src supplies the buffer where the data is move from.
 * @param max_length supplies the max bytes the call can move.
 * @return number of bytes this call moves.
 */
uint64_t moveUpTo(Buffer::Instance& dst, Buffer::Instance& src, uint64_t max_length) {
  ASSERT(src.length() > 0);
  if (dst.highWatermark() != 0) {
    if (dst.length() < dst.highWatermark()) {
      // Move until high watermark so that high watermark is not triggered.
      // However, if dst buffer is near high watermark, move 16K to avoid the small fragment move.
      max_length = std::min(max_length,
                            std::max<uint64_t>(FRAGMENT_SIZE, dst.highWatermark() - dst.length()));
    } else {
      // Move at most 16K if the dst buffer is over high watermark.
      max_length = std::min<uint64_t>(max_length, FRAGMENT_SIZE);
    }
  }
  uint64_t res = std::min(max_length, src.length());
  dst.move(src, res);
  return res;
}
} // namespace

const Network::Address::InstanceConstSharedPtr& IoHandleImpl::getCommonInternalAddress() {
  CONSTRUCT_ON_FIRST_USE(Network::Address::InstanceConstSharedPtr,
                         std::make_shared<const Network::Address::EnvoyInternalInstance>(
                             "internal_address_for_user_space_io_handle"));
}

IoHandleImpl::IoHandleImpl()
    : pending_received_data_([&]() -> void { this->onBelowLowWatermark(); },
                             [&]() -> void { this->onAboveHighWatermark(); }, []() -> void {}) {}

IoHandleImpl::~IoHandleImpl() {
  if (!closed_) {
    close();
  }
}

Api::IoCallUint64Result IoHandleImpl::close() {
  ASSERT(!closed_);
  if (!closed_) {
    if (peer_handle_) {
      ENVOY_LOG(trace, "socket {} close before peer {} closes.", static_cast<void*>(this),
                static_cast<void*>(peer_handle_));
      // Notify the peer we won't write more data. shutdown(WRITE).
      peer_handle_->setWriteEnd();
      // Notify the peer that we no longer accept data. shutdown(RD).
      peer_handle_->onPeerDestroy();
      peer_handle_ = nullptr;
    } else {
      ENVOY_LOG(trace, "socket {} close after peer closed.", static_cast<void*>(this));
    }
  }
  closed_ = true;
  return Api::ioCallUint64ResultNoError();
}

bool IoHandleImpl::isOpen() const { return !closed_; }

Api::IoCallUint64Result IoHandleImpl::readv(uint64_t max_length, Buffer::RawSlice* slices,
                                            uint64_t num_slice) {
  if (!isOpen()) {
    return {0, Api::IoErrorPtr(new Network::IoSocketError(SOCKET_ERROR_BADF),
                               Network::IoSocketError::deleteIoError)};
  }
  if (pending_received_data_.length() == 0) {
    if (receive_data_end_stream_) {
      return {0, Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError)};
    } else {
      return {0, Api::IoErrorPtr(Network::IoSocketError::getIoSocketEagainInstance(),
                                 Network::IoSocketError::deleteIoError)};
    }
  }
  // The read bytes can not exceed the provided buffer size or pending received size.
  const auto max_bytes_to_read = std::min(pending_received_data_.length(), max_length);
  uint64_t bytes_offset = 0;
  for (uint64_t i = 0; i < num_slice && bytes_offset < max_length; i++) {
    auto bytes_to_read_in_this_slice =
        std::min(max_bytes_to_read - bytes_offset, uint64_t(slices[i].len_));
    // Copy and drain, so pending_received_data_ always copy from offset 0.
    pending_received_data_.copyOut(0, bytes_to_read_in_this_slice, slices[i].mem_);
    pending_received_data_.drain(bytes_to_read_in_this_slice);
    bytes_offset += bytes_to_read_in_this_slice;
  }
  const auto bytes_read = bytes_offset;
  ASSERT(bytes_read <= max_bytes_to_read);
  ENVOY_LOG(trace, "socket {} readv {} bytes", static_cast<void*>(this), bytes_read);
  return {bytes_read, Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError)};
}

Api::IoCallUint64Result IoHandleImpl::read(Buffer::Instance& buffer,
                                           absl::optional<uint64_t> max_length_opt) {
  // Below value comes from Buffer::OwnedImpl::default_read_reservation_size_.
  uint64_t max_length = max_length_opt.value_or(MAX_FRAGMENT * FRAGMENT_SIZE);
  if (max_length == 0) {
    return Api::ioCallUint64ResultNoError();
  }
  if (!isOpen()) {
    return {0, Api::IoErrorPtr(new Network::IoSocketError(SOCKET_ERROR_BADF),
                               Network::IoSocketError::deleteIoError)};
  }
  if (pending_received_data_.length() == 0) {
    if (receive_data_end_stream_) {
      return {0, Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError)};
    } else {
      return {0, Api::IoErrorPtr(Network::IoSocketError::getIoSocketEagainInstance(),
                                 Network::IoSocketError::deleteIoError)};
    }
  }
  const uint64_t bytes_to_read = moveUpTo(buffer, pending_received_data_, max_length);
  return {bytes_to_read, Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError)};
}

Api::IoCallUint64Result IoHandleImpl::writev(const Buffer::RawSlice* slices, uint64_t num_slice) {
  // Empty input is allowed even though the peer is shutdown.
  bool is_input_empty = true;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      is_input_empty = false;
      break;
    }
  }
  if (is_input_empty) {
    return Api::ioCallUint64ResultNoError();
  };
  if (!isOpen()) {
    return {0, Api::IoErrorPtr(new Network::IoSocketError(SOCKET_ERROR_BADF),
                               Network::IoSocketError::deleteIoError)};
  }
  // Closed peer.
  if (!peer_handle_) {
    return {0, Api::IoErrorPtr(new Network::IoSocketError(SOCKET_ERROR_INVAL),
                               Network::IoSocketError::deleteIoError)};
  }
  // Error: write after close.
  if (peer_handle_->isPeerShutDownWrite()) {
    // TODO(lambdai): `EPIPE` or `ENOTCONN`.
    return {0, Api::IoErrorPtr(new Network::IoSocketError(SOCKET_ERROR_INVAL),
                               Network::IoSocketError::deleteIoError)};
  }
  // The peer is valid but temporarily does not accept new data. Likely due to flow control.
  if (!peer_handle_->isWritable()) {
    return {0, Api::IoErrorPtr(Network::IoSocketError::getIoSocketEagainInstance(),
                               Network::IoSocketError::deleteIoError)};
  }

  auto* const dest_buffer = peer_handle_->getWriteBuffer();
  // Write along with iteration. Buffer guarantee the fragment is always append-able.
  uint64_t bytes_written = 0;
  for (uint64_t i = 0; i < num_slice && !dest_buffer->highWatermarkTriggered(); i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      dest_buffer->add(slices[i].mem_, slices[i].len_);
      bytes_written += slices[i].len_;
    }
  }
  peer_handle_->setNewDataAvailable();
  ENVOY_LOG(trace, "socket {} writev {} bytes", static_cast<void*>(this), bytes_written);
  return {bytes_written, Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError)};
}

Api::IoCallUint64Result IoHandleImpl::write(Buffer::Instance& buffer) {
  // Empty input is allowed even though the peer is shutdown.
  if (buffer.length() == 0) {
    return Api::ioCallUint64ResultNoError();
  }
  if (!isOpen()) {
    return {0, Api::IoErrorPtr(new Network::IoSocketError(SOCKET_ERROR_BADF),
                               Network::IoSocketError::deleteIoError)};
  }
  // Closed peer.
  if (!peer_handle_) {
    return {0, Api::IoErrorPtr(new Network::IoSocketError(SOCKET_ERROR_INVAL),
                               Network::IoSocketError::deleteIoError)};
  }
  // Error: write after close.
  if (peer_handle_->isPeerShutDownWrite()) {
    // TODO(lambdai): `EPIPE` or `ENOTCONN`.
    return {0, Api::IoErrorPtr(new Network::IoSocketError(SOCKET_ERROR_INVAL),
                               Network::IoSocketError::deleteIoError)};
  }
  // The peer is valid but temporarily does not accept new data. Likely due to flow control.
  if (!peer_handle_->isWritable()) {
    return {0, Api::IoErrorPtr(Network::IoSocketError::getIoSocketEagainInstance(),
                               Network::IoSocketError::deleteIoError)};
  }
  const uint64_t max_bytes_to_write = buffer.length();
  const uint64_t total_bytes_to_write =
      moveUpTo(*peer_handle_->getWriteBuffer(), buffer,
               // Below value comes from Buffer::OwnedImpl::default_read_reservation_size_.
               MAX_FRAGMENT * FRAGMENT_SIZE);
  peer_handle_->setNewDataAvailable();
  ENVOY_LOG(trace, "socket {} write {} bytes of {}", static_cast<void*>(this), total_bytes_to_write,
            max_bytes_to_write);
  return {total_bytes_to_write, Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError)};
}

Api::IoCallUint64Result IoHandleImpl::sendmsg(const Buffer::RawSlice*, uint64_t, int,
                                              const Network::Address::Ip*,
                                              const Network::Address::Instance&) {
  return Network::IoSocketError::ioResultSocketInvalidAddress();
}

Api::IoCallUint64Result IoHandleImpl::recvmsg(Buffer::RawSlice*, const uint64_t, uint32_t,
                                              RecvMsgOutput&) {
  return Network::IoSocketError::ioResultSocketInvalidAddress();
}

Api::IoCallUint64Result IoHandleImpl::recvmmsg(RawSliceArrays&, uint32_t, RecvMsgOutput&) {
  return Network::IoSocketError::ioResultSocketInvalidAddress();
}

Api::IoCallUint64Result IoHandleImpl::recv(void* buffer, size_t length, int flags) {
  if (!isOpen()) {
    return {0, Api::IoErrorPtr(new Network::IoSocketError(SOCKET_ERROR_BADF),
                               Network::IoSocketError::deleteIoError)};
  }
  // No data and the writer closed.
  if (pending_received_data_.length() == 0) {
    if (receive_data_end_stream_) {
      return {0, Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError)};
    } else {
      return {0, Api::IoErrorPtr(Network::IoSocketError::getIoSocketEagainInstance(),
                                 Network::IoSocketError::deleteIoError)};
    }
  }
  // Specify uint64_t since the latter length may not have the same type.
  const auto max_bytes_to_read = std::min<uint64_t>(pending_received_data_.length(), length);
  pending_received_data_.copyOut(0, max_bytes_to_read, buffer);
  if (!(flags & MSG_PEEK)) {
    pending_received_data_.drain(max_bytes_to_read);
  }
  return {max_bytes_to_read, Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError)};
}

bool IoHandleImpl::supportsMmsg() const { return false; }

bool IoHandleImpl::supportsUdpGro() const { return false; }

Api::SysCallIntResult IoHandleImpl::bind(Network::Address::InstanceConstSharedPtr) {
  return makeInvalidSyscallResult();
}

Api::SysCallIntResult IoHandleImpl::listen(int) { return makeInvalidSyscallResult(); }

Network::IoHandlePtr IoHandleImpl::accept(struct sockaddr*, socklen_t*) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

Api::SysCallIntResult IoHandleImpl::connect(Network::Address::InstanceConstSharedPtr) {
  // Buffered Io handle should always be considered as connected.
  // Use write or read to determine if peer is closed.
  return {0, 0};
}

Api::SysCallIntResult IoHandleImpl::setOption(int, int, const void*, socklen_t) {
  return makeInvalidSyscallResult();
}

Api::SysCallIntResult IoHandleImpl::getOption(int, int, void*, socklen_t*) {
  return makeInvalidSyscallResult();
}

Api::SysCallIntResult IoHandleImpl::ioctl(unsigned long, void*, unsigned long, void*, unsigned long,
                                          unsigned long*) {
  return makeInvalidSyscallResult();
}

Api::SysCallIntResult IoHandleImpl::setBlocking(bool) { return makeInvalidSyscallResult(); }

absl::optional<int> IoHandleImpl::domain() { return absl::nullopt; }

Network::Address::InstanceConstSharedPtr IoHandleImpl::localAddress() {
  return IoHandleImpl::getCommonInternalAddress();
}

Network::Address::InstanceConstSharedPtr IoHandleImpl::peerAddress() {
  return IoHandleImpl::getCommonInternalAddress();
}

void IoHandleImpl::initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                                       Event::FileTriggerType trigger, uint32_t events) {
  ASSERT(user_file_event_ == nullptr, "Attempting to initialize two `file_event_` for the same "
                                      "file descriptor. This is not allowed.");
  ASSERT(trigger != Event::FileTriggerType::Level, "Native level trigger is not supported.");
  user_file_event_ = std::make_unique<FileEventImpl>(dispatcher, cb, events, *this);
}

Network::IoHandlePtr IoHandleImpl::duplicate() {
  // duplicate() is supposed to be used on listener io handle while this implementation doesn't
  // support listen.
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

void IoHandleImpl::activateFileEvents(uint32_t events) {
  if (user_file_event_) {
    user_file_event_->activate(events);
  } else {
    ENVOY_BUG(false, "Null user_file_event_");
  }
}

void IoHandleImpl::enableFileEvents(uint32_t events) {
  if (user_file_event_) {
    user_file_event_->setEnabled(events);
  } else {
    ENVOY_BUG(false, "Null user_file_event_");
  }
}

void IoHandleImpl::resetFileEvents() { user_file_event_.reset(); }

Api::SysCallIntResult IoHandleImpl::shutdown(int how) {
  // Support only shutdown write.
  ASSERT(how == ENVOY_SHUT_WR);
  ASSERT(!closed_);
  if (!write_shutdown_) {
    ASSERT(peer_handle_);
    // Notify the peer we won't write more data.
    peer_handle_->setWriteEnd();
    write_shutdown_ = true;
  }
  return {0, 0};
}
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
