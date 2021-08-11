#pragma once

#include "envoy/api/io_error.h"
#include "envoy/api/os_sys_calls.h"
#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/io_handle.h"

#include "source/common/common/logger.h"
#include "source/common/network/io_socket_error_impl.h"

namespace Envoy {
namespace Network {

/**
 * IoHandle derivative for sockets.
 */
class IoSocketHandleImpl : public IoHandle, protected Logger::Loggable<Logger::Id::io> {
public:
  explicit IoSocketHandleImpl(os_fd_t fd = INVALID_SOCKET, bool socket_v6only = false,
                              absl::optional<int> domain = absl::nullopt)
      : fd_(fd), socket_v6only_(socket_v6only), domain_(domain) {}

  // Close underlying socket if close() hasn't been call yet.
  ~IoSocketHandleImpl() override;

  // TODO(sbelair2)  To be removed when the fd is fully abstracted from clients.
  os_fd_t fdDoNotUse() const override { return fd_; }

  Api::IoCallUint64Result close() override;

  bool isOpen() const override;

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

  bool supportsMmsg() const override;
  bool supportsUdpGro() const override;

  Api::SysCallIntResult bind(Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult listen(int backlog) override;
  IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;
  Api::SysCallIntResult connect(Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult setOption(int level, int optname, const void* optval,
                                  socklen_t optlen) override;
  Api::SysCallIntResult getOption(int level, int optname, void* optval, socklen_t* optlen) override;
  Api::SysCallIntResult ioctl(unsigned long control_code, void* in_buffer,
                              unsigned long in_buffer_len, void* out_buffer,
                              unsigned long out_buffer_len, unsigned long* bytes_returned) override;
  Api::SysCallIntResult setBlocking(bool blocking) override;
  absl::optional<int> domain() override;
  Address::InstanceConstSharedPtr localAddress() override;
  Address::InstanceConstSharedPtr peerAddress() override;
  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override;

  IoHandlePtr duplicate() override;

  void activateFileEvents(uint32_t events) override;
  void enableFileEvents(uint32_t events) override;

  void resetFileEvents() override { file_event_.reset(); }

  Api::SysCallIntResult shutdown(int how) override;
  absl::optional<std::chrono::milliseconds> lastRoundTripTime() override;

protected:
  // Converts a SysCallSizeResult to IoCallUint64Result.
  template <typename T>
  Api::IoCallUint64Result sysCallResultToIoCallResult(const Api::SysCallResult<T>& result) {
    if (result.return_value_ >= 0) {
      // Return nullptr as IoError upon success.
      return Api::IoCallUint64Result(result.return_value_,
                                     Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError));
    }
    RELEASE_ASSERT(result.errno_ != SOCKET_ERROR_INVAL, "Invalid argument passed in.");
    return Api::IoCallUint64Result(
        /*rc=*/0,
        (result.errno_ == SOCKET_ERROR_AGAIN
             // EAGAIN is frequent enough that its memory allocation should be avoided.
             ? Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                               IoSocketError::deleteIoError)
             : Api::IoErrorPtr(new IoSocketError(result.errno_), IoSocketError::deleteIoError)));
  }

  os_fd_t fd_;
  int socket_v6only_{false};
  const absl::optional<int> domain_;
  Event::FileEventPtr file_event_{nullptr};

  // The minimum cmsg buffer size to filled in destination address, packets dropped and gso
  // size when receiving a packet. It is possible for a received packet to contain both IPv4
  // and IPV6 addresses.
  const size_t cmsg_space_{CMSG_SPACE(sizeof(int)) + CMSG_SPACE(sizeof(struct in_pktinfo)) +
                           CMSG_SPACE(sizeof(struct in6_pktinfo)) + CMSG_SPACE(sizeof(uint16_t))};
};
} // namespace Network
} // namespace Envoy
