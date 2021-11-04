#pragma once

#include "envoy/api/io_error.h"
#include "envoy/api/os_sys_calls.h"
#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/io_handle.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Network {

/**
 * IoHandle derivative for win32 emulated edge sockets.
 */
class Win32SocketHandleImpl : public IoSocketHandleImpl {
public:
  explicit Win32SocketHandleImpl(os_fd_t fd = INVALID_SOCKET, bool socket_v6only = false,
                                 absl::optional<int> domain = absl::nullopt)
      : peek_buffer_(std::make_unique<Buffer::OwnedImpl>()),
        IoSocketHandleImpl(fd, socket_v6only, domain) {}

  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;
  Api::IoCallUint64Result read(Buffer::Instance& buffer,
                               absl::optional<uint64_t> max_length) override;

  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;

  Api::IoCallUint64Result write(Buffer::Instance& buffer) override;

  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice, int flags,
                                  const Address::Ip* self_ip,
                                  const Address::Instance& peer_address) override;

  Api::IoCallUint64Result recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                  uint32_t self_port, RecvMsgOutput& output) override;

  Api::IoCallUint64Result recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                   RecvMsgOutput& output) override;
  Api::IoCallUint64Result recv(void* buffer, size_t length, int flags) override;

  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override;
  void enableFileEvents(uint32_t events) override;
private:
  void reEnableEventBasedOnIOResult(const Api::IoCallUint64Result& result, uint32_t event);

  // For windows mimic MSG_PEEK
  std::unique_ptr<Buffer::Instance> peek_buffer_;

  Api::IoCallUint64Result drainToPeekBuffer();
  Api::IoCallUint64Result readFromPeekBuffer(void* buffer, size_t length);
  Api::IoCallUint64Result readFromPeekBuffer(Buffer::Instance& buffer, size_t length);
  Api::IoCallUint64Result readvFromPeekBuffer(uint64_t max_length, Buffer::RawSlice* slices,
                                              uint64_t num_slice);
  Api::IoCallUint64Result peekFromPeekBuffer(void* buffer, size_t length);

  Api::IoCallUint64Result readvImpl(uint64_t max_length, Buffer::RawSlice* slices,
                                    uint64_t num_slice);
};
} // namespace Network
} // namespace Envoy
