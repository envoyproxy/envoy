#include "source/common/network/win32_socket_handle_impl.h"

#include "envoy/buffer/buffer.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/utility.h"
#include "source/common/event/file_event_impl.h"
#include "source/common/network/address_impl.h"

#include "absl/container/fixed_array.h"
#include "absl/types/optional.h"

using Envoy::Api::SysCallIntResult;
using Envoy::Api::SysCallSizeResult;

namespace Envoy {
namespace Network {

Api::IoCallUint64Result Win32SocketHandleImpl::readv(uint64_t max_length, Buffer::RawSlice* slices,
                                                     uint64_t num_slice) {
  if (peek_buffer_->length() == 0) {
    auto result = IoSocketHandleImpl::readv(max_length, slices, num_slice);
    reEnableEventBasedOnIOResult(result, Event::FileReadyType::Read);
    return result;
  }

  return readvFromPeekBuffer(max_length, slices, num_slice);
}

Api::IoCallUint64Result Win32SocketHandleImpl::read(Buffer::Instance& buffer,
                                                    absl::optional<uint64_t> max_length_opt) {
  if (peek_buffer_->length() == 0) {
    auto result = IoSocketHandleImpl::read(buffer, max_length_opt);
    reEnableEventBasedOnIOResult(result, Event::FileReadyType::Read);
    return result;
  }

  return readFromPeekBuffer(buffer, max_length_opt.value_or(UINT64_MAX));
}

Api::IoCallUint64Result Win32SocketHandleImpl::writev(const Buffer::RawSlice* slices,
                                                      uint64_t num_slice) {
  auto result = IoSocketHandleImpl::writev(slices, num_slice);
  reEnableEventBasedOnIOResult(result, Event::FileReadyType::Write);
  return result;
}

Api::IoCallUint64Result Win32SocketHandleImpl::write(Buffer::Instance& buffer) {
  Api::IoCallUint64Result result = IoSocketHandleImpl::write(buffer);
  reEnableEventBasedOnIOResult(result, Event::FileReadyType::Write);
  return result;
}

Api::IoCallUint64Result Win32SocketHandleImpl::sendmsg(const Buffer::RawSlice* slices,
                                                       uint64_t num_slice, int flags,
                                                       const Address::Ip* self_ip,
                                                       const Address::Instance& peer_address) {

  Api::IoCallUint64Result result =
      IoSocketHandleImpl::sendmsg(slices, num_slice, flags, self_ip, peer_address);
  reEnableEventBasedOnIOResult(result, Event::FileReadyType::Write);
  return result;
}

Api::IoCallUint64Result Win32SocketHandleImpl::recvmsg(Buffer::RawSlice* slices,
                                                       const uint64_t num_slice, uint32_t self_port,
                                                       RecvMsgOutput& output) {
  Api::IoCallUint64Result result =
      IoSocketHandleImpl::recvmsg(slices, num_slice, self_port, output);
  reEnableEventBasedOnIOResult(result, Event::FileReadyType::Read);
  return result;
}

Api::IoCallUint64Result Win32SocketHandleImpl::recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                                        RecvMsgOutput& output) {
  Api::IoCallUint64Result result = IoSocketHandleImpl::recvmmsg(slices, self_port, output);
  reEnableEventBasedOnIOResult(result, Event::FileReadyType::Read);
  return result;
}

Api::IoCallUint64Result Win32SocketHandleImpl::recv(void* buffer, size_t length, int flags) {
  if (flags & MSG_PEEK) {
    // can a remote OOM us now that we are not protected by readDisable?
    Api::IoCallUint64Result peek_result = drainToPeekBuffer();

    //  Some fatal error happened
    if (!peek_result.wouldBlock()) {
      return peek_result;
    }

    // No data available, register read again.
    if (peek_result.wouldBlock() && peek_buffer_->length() == 0) {
      file_event_->registerEventIfEmulatedEdge(Event::FileReadyType::Read);
      return peek_result;
    }

    Api::IoCallUint64Result result = peekFromPeekBuffer(buffer, length);
    if (peek_buffer_->length() < length) {
      file_event_->registerEventIfEmulatedEdge(Event::FileReadyType::Read);
    } else {
      // This means that our peak buffer has more data than what the user
      // wanted. Return the slice to the caller.
      // How can the caller (v2 proxy protocol inspector) reactivate the events again here?
    }
    return result;
  }
  if (peek_buffer_->length() == 0) {
    Api::IoCallUint64Result result = IoSocketHandleImpl::recv(buffer, length, flags);
    reEnableEventBasedOnIOResult(result, Event::FileReadyType::Read);
    return result;
  } else {
    return readFromPeekBuffer(buffer, length);
  }
}

void Win32SocketHandleImpl::reEnableEventBasedOnIOResult(const Api::IoCallUint64Result& result,
                                                         uint32_t event) {
  if (result.wouldBlock() && file_event_) {
    file_event_->registerEventIfEmulatedEdge(event);
  }
}

Api::IoCallUint64Result Win32SocketHandleImpl::drainToPeekBuffer() {
  while (true) {
    Buffer::OwnedImpl read_buffer;
    Buffer::Reservation reservation = read_buffer.reserveForRead();
    Api::IoCallUint64Result result = IoSocketHandleImpl::readv(
        reservation.length(), reservation.slices(), reservation.numSlices());
    uint64_t bytes_to_commit = result.ok() ? result.return_value_ : 0;
    reservation.commit(bytes_to_commit);
    peek_buffer_->add(read_buffer);
    if (!result.ok() || bytes_to_commit == 0) {
      return result;
    }
  }
}

Api::IoCallUint64Result Win32SocketHandleImpl::readFromPeekBuffer(void* buffer, size_t length) {
  uint64_t copy_size = std::min(peek_buffer_->length(), static_cast<uint64_t>(length));
  peek_buffer_->copyOut(0, copy_size, buffer);
  peek_buffer_->drain(copy_size);
  return Api::IoCallUint64Result(copy_size, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
}

Api::IoCallUint64Result Win32SocketHandleImpl::readvFromPeekBuffer(uint64_t max_length,
                                                                   Buffer::RawSlice* slices,
                                                                   uint64_t num_slice) {
  uint64_t total_length_to_read = std::min(max_length, peek_buffer_->length());
  uint64_t num_slices_to_read = 0;
  uint64_t num_bytes_to_read = 0;
  for (; num_slices_to_read < num_slice && num_bytes_to_read < total_length_to_read;
       num_slices_to_read++) {
    auto length_to_copy = std::min(static_cast<uint64_t>(slices[num_slices_to_read].len_),
                                   total_length_to_read - num_bytes_to_read);
    peek_buffer_->copyOut(num_bytes_to_read, length_to_copy, slices[num_slices_to_read].mem_);
    num_bytes_to_read += length_to_copy;
  }
  peek_buffer_->drain(num_bytes_to_read);
  return Api::IoCallUint64Result(num_bytes_to_read, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
}

Api::IoCallUint64Result Win32SocketHandleImpl::readFromPeekBuffer(Buffer::Instance& buffer,
                                                                  size_t length) {
  auto lenght_to_move = std::min(peek_buffer_->length(), static_cast<uint64_t>(length));
  buffer.move(*peek_buffer_, lenght_to_move);
  peek_buffer_->drain(lenght_to_move);
  return Api::IoCallUint64Result(lenght_to_move, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
}

Api::IoCallUint64Result Win32SocketHandleImpl::peekFromPeekBuffer(void* buffer, size_t length) {
  uint64_t copy_size = std::min(peek_buffer_->length(), static_cast<uint64_t>(length));
  peek_buffer_->copyOut(0, copy_size, buffer);
  return Api::IoCallUint64Result(copy_size, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
}

void Win32SocketHandleImpl::initializeFileEvent(Event::Dispatcher& dispatcher,
                                                Event::FileReadyCb cb,
                                                Event::FileTriggerType trigger, uint32_t events) {
  IoSocketHandleImpl::initializeFileEvent(dispatcher, cb, trigger, events);
  if ((events & Event::FileReadyType::Read) && peek_buffer_->length() > 0) {
    activateFileEvents(Event::FileReadyType::Read);
  }
}

void Win32SocketHandleImpl::enableFileEvents(uint32_t events) {
  IoSocketHandleImpl::enableFileEvents(events);
  if ((events & Event::FileReadyType::Read) && peek_buffer_->length() > 0) {
    activateFileEvents(Event::FileReadyType::Read);
  }
}

} // namespace Network
} // namespace Envoy
