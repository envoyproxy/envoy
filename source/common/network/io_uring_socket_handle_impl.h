#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/io_handle.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/io/io_uring.h"

namespace Envoy {

namespace Network {

class IoUringSocketHandleImpl;

enum class RequestType { Accept, Connect, Read, Write, Close, Cancel, Unknown };

using IoUringSocketHandleImplOptRef =
    absl::optional<std::reference_wrapper<IoUringSocketHandleImpl>>;

struct Request {
  RequestType type_{RequestType::Unknown};
  struct iovec* iov_{nullptr};
  std::unique_ptr<uint8_t[]> buf_{};
  struct sockaddr remote_addr_ {};
  socklen_t remote_addr_len_{sizeof(remote_addr_)};
};

/**
 * IoHandle derivative for sockets.
 */
class IoUringSocketHandleImpl final : public IoHandle, protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringSocketHandleImpl(const uint32_t read_buffer_size, const Io::IoUringFactory&,
                          os_fd_t fd = INVALID_SOCKET, bool socket_v6only = false,
                          absl::optional<int> domain = absl::nullopt);
  ~IoUringSocketHandleImpl() override;

  // Network::IoHandle
  // TODO(rojkov)  To be removed when the fd is fully abstracted from clients.
  os_fd_t fdDoNotUse() const override { return fd_; }
  Api::IoCallUint64Result close() override;
  bool isOpen() const override;
  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;
  Api::IoCallUint64Result read(Buffer::Instance& buffer,
                               absl::optional<uint64_t> max_length_opt) override;
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
  Api::SysCallIntResult ioctl(unsigned long, void*, unsigned long, void*, unsigned long,
                              unsigned long*) override;
  Api::SysCallIntResult setBlocking(bool blocking) override;
  absl::optional<int> domain() override;
  Address::InstanceConstSharedPtr localAddress() override;
  Address::InstanceConstSharedPtr peerAddress() override;
  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override;
  IoHandlePtr duplicate() override;
  void activateFileEvents(uint32_t events) override;
  void enableFileEvents(uint32_t events) override;
  void resetFileEvents() override;
  Api::SysCallIntResult shutdown(int how) override;
  absl::optional<std::chrono::milliseconds> lastRoundTripTime() override { return absl::nullopt; }
  absl::optional<uint64_t> congestionWindowInBytes() const override { return absl::nullopt; }
  absl::optional<std::string> interfaceName() override;

private:
  Io::IoUring& ioUring();
  void addAcceptRequest();
  void addReadRequest();
  void onRequestCompletion(Request* request, int32_t result);

  const uint32_t read_buffer_size_;
  const Io::IoUringFactory& io_uring_factory_;
  os_fd_t fd_;
  int socket_v6only_;
  const absl::optional<int> domain_;

  OptRef<Io::IoUring> io_uring_{absl::nullopt};
  bool is_listener_{false};
  Event::FileReadyCb cb_;
  os_fd_t connection_fd_{INVALID_SOCKET};
  struct sockaddr connection_addr_;
  socklen_t connection_addr_len_;
  Request* accept_req_{nullptr};
  int32_t connect_ret_{0};
  Buffer::OwnedImpl read_buf_;
  int32_t read_ret_{0};
  Request* read_req_{nullptr};
  bool is_read_enabled_{true};
  int32_t write_ret_{0};
  bool is_write_added_{false};
  bool remote_closed_{false};
};

} // namespace Network
} // namespace Envoy
