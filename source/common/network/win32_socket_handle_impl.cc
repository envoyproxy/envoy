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
  auto result = IoSocketHandleImpl::readv(max_length, slices, num_slice);
  reEnableEventBasedOnIOResult(result, Event::FileReadyType::Read);
  return result;
}

Api::IoCallUint64Result Win32SocketHandleImpl::read(Buffer::Instance& buffer,
                                                    absl::optional<uint64_t> max_length_opt) {
  auto result = IoSocketHandleImpl::read(buffer, max_length_opt);
  reEnableEventBasedOnIOResult(result, Event::FileReadyType::Read);
  return result;
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

  Api::IoCallUint64Result result = IoSocketHandleImpl::recv(buffer, length, flags);
  reEnableEventBasedOnIOResult(result, Event::FileReadyType::Read);
  return result;
}

void Win32SocketHandleImpl::reEnableEventBasedOnIOResult(const Api::IoCallUint64Result& result,
                                                         uint32_t event) {
  if (result.wouldBlock() && file_event_) {
    file_event_->registerEventIfEmulatedEdge(event);
  }
}

} // namespace Network
} // namespace Envoy
