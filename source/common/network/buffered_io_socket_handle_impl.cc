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

Api::IoCallUint64Result BufferedIoSocketHandleImpl::close() {
  ASSERT(!closed_);
  if (!peer_closed_) {
    ASSERT(writable_peer_);
    // Notify the peer we won't write more data. shutdown(WRITE).
    writable_peer_->setWriteEnd();
    // Notify the peer that we no longer accept data. shutdown(RD).
    writable_peer_->onPeerDestroy();
    writable_peer_->maybeSetNewData();
    writable_peer_ = nullptr;
    peer_closed_ = true;
  }
  closed_ = true;
  return IoSocketError::ioResultSocketInvalidAddress();
}

bool BufferedIoSocketHandleImpl::isOpen() const { return !closed_; }

Api::IoCallUint64Result BufferedIoSocketHandleImpl::readv(uint64_t, Buffer::RawSlice*, uint64_t) {
  return IoSocketError::ioResultSocketInvalidAddress();
}

Api::IoCallUint64Result BufferedIoSocketHandleImpl::writev(const Buffer::RawSlice*, uint64_t) {
  return IoSocketError::ioResultSocketInvalidAddress();
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
  if (flags | MSG_PEEK) {
    // No data and the writer closed.
    if (owned_buffer_.length() == 0) {

      if (read_end_stream_) {
        return sysCallResultToIoCallResult(Api::SysCallSizeResult{-1, SOCKET_ERROR_INTR});
      } else {
        return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                                   IoSocketError::deleteIoError)};
      }
    } else {
      auto min_len = std::min(owned_buffer_.length(), length);
      owned_buffer_.copyOut(0, min_len, buffer);
      return {min_len, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})};
    }
  } else {
    // TODO(lambdai): implement non-PEEK for proxy_protocol listener filter.
    return IoSocketError::ioResultSocketInvalidAddress();
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
  return makeInvalidSyscall();
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
                                                                Event::FileTriggerType,
                                                                uint32_t events) {
  return std::make_unique<Event::TimerWrappedFileEventImpl>(
      dispatcher.createSchedulableCallback([cb, events]() -> void { cb(events); }));
}

Api::SysCallIntResult BufferedIoSocketHandleImpl::shutdown(int how) {
  if ((how | ENVOY_SHUT_WR) || (how | ENVOY_SHUT_RDWR)) {
    ASSERT(!closed_);
    if (!peer_closed_) {
      ASSERT(writable_peer_);
      // Notify the peer we won't write more data. shutdown(WRITE).
      writable_peer_->setWriteEnd();
      writable_peer_->maybeSetNewData();
      writable_peer_ = nullptr;
      peer_closed_ = true;
    }
  }
  // TODO(lambdai): return correct error code.
  return {0, 0};
}

} // namespace Network
} // namespace Envoy