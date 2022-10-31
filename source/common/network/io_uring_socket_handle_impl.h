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
  IoUringSocketHandleImplOptRef iohandle_{absl::nullopt};
  RequestType type_{RequestType::Unknown};
  struct iovec* iov_{nullptr};
  os_fd_t fd_{-1};
  std::unique_ptr<uint8_t[]> buf_{};
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
  // FileEventAdapter adapts `io_uring` to libevent.
  class FileEventAdapter {
  public:
    FileEventAdapter(const Io::IoUringFactory& io_uring_factory)
        : io_uring_factory_(io_uring_factory) {}
    void initialize(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                    Event::FileTriggerType trigger, uint32_t events);

  private:
    void onFileEvent();
    void onRequestCompletion(const Request& req, int32_t result);

    const Io::IoUringFactory& io_uring_factory_;
    Event::FileReadyCb cb_;
    Event::FileEventPtr file_event_{nullptr};
  };

  void addAcceptRequest();
  void addReadRequest();
  // Checks if the io handle is the one that registered eventfd with `io_uring`.
  // An io handle can be a leader in two cases:
  //   1. it's a server socket accepting new connections;
  //   2. it's a client socket about to connect to a remote socket, but created
  //      in a thread without properly initialized `io_uring`.
  bool isLeader() const { return file_event_adapter_ != nullptr; }

  const uint32_t read_buffer_size_;
  const Io::IoUringFactory& io_uring_factory_;
  os_fd_t fd_;
  int socket_v6only_;
  const absl::optional<int> domain_;

  Event::FileReadyCb cb_;
  Buffer::OwnedImpl read_buf_;
  int32_t bytes_to_read_{0};
  Request* read_req_{nullptr};
  bool is_read_enabled_{true};
  int32_t bytes_already_wrote_{0};
  bool is_write_added_{false};
  std::unique_ptr<FileEventAdapter> file_event_adapter_{nullptr};
  bool remote_closed_{false};

  // For accept
  struct sockaddr remote_addr_;
  socklen_t remote_addr_len_{sizeof(remote_addr_)};
  bool is_accept_added_{false};
  os_fd_t connection_fd_{INVALID_SOCKET};
};

} // namespace Network
} // namespace Envoy
