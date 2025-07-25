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
      : IoSocketHandleImpl(fd, socket_v6only, domain) {}

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
                                  uint32_t self_port,
                                  const IoHandle::UdpSaveCmsgConfig& udp_save_cmsg_config,
                                  RecvMsgOutput& output) override;

  Api::IoCallUint64Result recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                   const IoHandle::UdpSaveCmsgConfig& udp_save_cmsg_config,
                                   RecvMsgOutput& output) override;
  Api::IoCallUint64Result recv(void* buffer, size_t length, int flags) override;

  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override;
  void enableFileEvents(uint32_t events) override;

private:
  void reEnableEventBasedOnIOResult(const Api::IoCallUint64Result& result, uint32_t event);

  // On Windows we use the MSG_PEEK on recv instead of peeking the socket
  // we drain the socket to memory. Subsequent read calls need to read
  // first from the class buffer and then go to the underlying socket.

  // Implement the peek logic of recv for readability purposes
  Api::IoCallUint64Result emulatePeek(void* buffer, size_t length);

  /**
   * Drain the socket into `peek_buffer_`.
   * @param length is the desired length of data drained into the `peek_buffer_`.
   * @return the actual length of data drained into the `peek_buffer_`.
   */
  Api::IoCallUint64Result drainToPeekBuffer(size_t length);

  // Useful functions to read from the peek buffer based on
  // the signatures of readv/read/recv OS socket functions.
  Api::IoCallUint64Result readFromPeekBuffer(void* buffer, size_t length);
  Api::IoCallUint64Result readFromPeekBuffer(Buffer::Instance& buffer, size_t length);
  Api::IoCallUint64Result readvFromPeekBuffer(uint64_t max_length, Buffer::RawSlice* slices,
                                              uint64_t num_slice);
  Api::IoCallUint64Result peekFromPeekBuffer(void* buffer, size_t length);

  // For windows mimic MSG_PEEK
  Buffer::OwnedImpl peek_buffer_;
};
} // namespace Network
} // namespace Envoy
