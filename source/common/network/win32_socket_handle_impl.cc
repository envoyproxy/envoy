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
  if (peek_buffer_.length() == 0) {
    auto result = IoSocketHandleImpl::readv(max_length, slices, num_slice);
    reEnableEventBasedOnIOResult(result, Event::FileReadyType::Read);
    return result;
  }

  return readvFromPeekBuffer(max_length, slices, num_slice);
}

Api::IoCallUint64Result Win32SocketHandleImpl::read(Buffer::Instance& buffer,
                                                    absl::optional<uint64_t> max_length_opt) {
  if (peek_buffer_.length() == 0) {
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
    return peek(buffer, length);
  }

  if (peek_buffer_.length() == 0) {
    Api::IoCallUint64Result result = IoSocketHandleImpl::recv(buffer, length, flags);
    reEnableEventBasedOnIOResult(result, Event::FileReadyType::Read);
    return result;
  } else {
    return readFromPeekBuffer(buffer, length);
  }
}

Api::IoCallUint64Result Win32SocketHandleImpl::peek(void* buffer, size_t length) {

  // if the `peek_buffer_` has already enough data we can skip reading
  // from the wire.
  if (length > peek_buffer_.length()) {
    // The caller is responsible for calling with the larger size
    // in cases it needs to do so it can't rely on transparent event activation.
    // So no in this case we should activate read again unless the read blocked.
    Api::IoCallUint64Result peek_result = drainToPeekBuffer(length);

    //  Some error happened
    if (!peek_result.ok()) {
      if (peek_result.wouldBlock() && file_event_) {
        file_event_->registerEventIfEmulatedEdge(Event::FileReadyType::Read);
        if (peek_buffer_.length() == 0) {
          return peek_result;
        }
      } else {
        return peek_result;
      }
    }
  }

  return peekFromPeekBuffer(buffer, length);
}

void Win32SocketHandleImpl::reEnableEventBasedOnIOResult(const Api::IoCallUint64Result& result,
                                                         uint32_t event) {
  if (result.wouldBlock() && file_event_) {
    file_event_->registerEventIfEmulatedEdge(event);
  }
}

Api::IoCallUint64Result Win32SocketHandleImpl::drainToPeekBuffer(size_t length) {
  size_t total_bytes_read = 0;
  while (peek_buffer_.length() < length) {
    Buffer::Reservation reservation = peek_buffer_.reserveForRead();
    auto bytes_to_read = std::min((length - total_bytes_read), reservation.length());
    Api::IoCallUint64Result result =
        IoSocketHandleImpl::readv(bytes_to_read, reservation.slices(), reservation.numSlices());
    uint64_t bytes_to_commit = result.ok() ? result.return_value_ : 0;
    reservation.commit(bytes_to_commit);
    total_bytes_read += bytes_to_commit;
    if (!result.ok() || bytes_to_commit == 0) {
      return result;
    }
  }
  return Api::IoCallUint64Result(total_bytes_read, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
}

Api::IoCallUint64Result Win32SocketHandleImpl::readFromPeekBuffer(void* buffer, size_t length) {
  uint64_t copy_size = std::min(peek_buffer_.length(), static_cast<uint64_t>(length));
  peek_buffer_.copyOut(0, copy_size, buffer);
  peek_buffer_.drain(copy_size);
  return Api::IoCallUint64Result(copy_size, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
}

Api::IoCallUint64Result Win32SocketHandleImpl::readvFromPeekBuffer(uint64_t max_length,
                                                                   Buffer::RawSlice* slices,
                                                                   uint64_t num_slice) {
  uint64_t bytes_read = peek_buffer_.copyOutToSlices(max_length, slices, num_slice);
  peek_buffer_.drain(bytes_read);
  return Api::IoCallUint64Result(bytes_read, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
}

Api::IoCallUint64Result Win32SocketHandleImpl::readFromPeekBuffer(Buffer::Instance& buffer,
                                                                  size_t length) {
  auto lenght_to_move = std::min(peek_buffer_.length(), static_cast<uint64_t>(length));
  buffer.move(peek_buffer_, lenght_to_move);
  return Api::IoCallUint64Result(lenght_to_move, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
}

Api::IoCallUint64Result Win32SocketHandleImpl::peekFromPeekBuffer(void* buffer, size_t length) {
  uint64_t copy_size = std::min(peek_buffer_.length(), static_cast<uint64_t>(length));
  peek_buffer_.copyOut(0, copy_size, buffer);
  return Api::IoCallUint64Result(copy_size, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
}

void Win32SocketHandleImpl::initializeFileEvent(Event::Dispatcher& dispatcher,
                                                Event::FileReadyCb cb,
                                                Event::FileTriggerType trigger, uint32_t events) {
  IoSocketHandleImpl::initializeFileEvent(dispatcher, cb, trigger, events);
  if ((events & Event::FileReadyType::Read) && peek_buffer_.length() > 0) {
    activateFileEvents(Event::FileReadyType::Read);
  }
}

void Win32SocketHandleImpl::enableFileEvents(uint32_t events) {
  IoSocketHandleImpl::enableFileEvents(events);
  if ((events & Event::FileReadyType::Read) && peek_buffer_.length() > 0) {
    activateFileEvents(Event::FileReadyType::Read);
  }
}

} // namespace Network
} // namespace Envoy
