#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/io/io_uring.h"
#include "envoy/network/io_handle.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/network/io_socket_handle_base_impl.h"

namespace Envoy {

namespace Network {

class IoUringSocketHandleImpl;

using IoUringSocketHandleImplOptRef =
    absl::optional<std::reference_wrapper<IoUringSocketHandleImpl>>;

enum class IoUringSocketType {
  Unknown,
  Accept,
  Server,
  Client,
};

/**
 * IoHandle derivative for sockets.
 */
class IoUringSocketHandleImpl : public IoSocketHandleBaseImpl {
public:
  IoUringSocketHandleImpl(Io::IoUringWorkerFactory& io_uring_worker_factory,
                          os_fd_t fd = INVALID_SOCKET, bool socket_v6only = false,
                          absl::optional<int> domain = absl::nullopt,
                          bool is_server_socket = false);
  ~IoUringSocketHandleImpl() override;

  Api::IoCallUint64Result close() override;
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
                                  uint32_t self_port,
                                  const IoHandle::UdpSaveCmsgConfig& udp_save_cmsg_config,
                                  RecvMsgOutput& output) override;
  Api::IoCallUint64Result recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                   const IoHandle::UdpSaveCmsgConfig& udp_save_cmsg_config,
                                   RecvMsgOutput& output) override;
  Api::IoCallUint64Result recv(void* buffer, size_t length, int flags) override;
  Api::SysCallIntResult bind(Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult listen(int backlog) override;
  IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;
  Api::SysCallIntResult connect(Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult getOption(int level, int optname, void* optval, socklen_t* optlen) override;
  IoHandlePtr duplicate() override;
  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override;
  void activateFileEvents(uint32_t events) override;
  void enableFileEvents(uint32_t events) override;
  void resetFileEvents() override;
  Api::SysCallIntResult shutdown(int how) override;

protected:
  std::string ioUringSocketTypeStr() const {
    switch (io_uring_socket_type_) {
    case IoUringSocketType::Unknown:
      return "unknown";
    case IoUringSocketType::Accept:
      return "accept";
    case IoUringSocketType::Client:
      return "client";
    case IoUringSocketType::Server:
      return "server";
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  Io::IoUringWorkerFactory& io_uring_worker_factory_;
  IoUringSocketType io_uring_socket_type_;
  OptRef<Io::IoUringSocket> io_uring_socket_{absl::nullopt};

  Event::FileEventPtr file_event_{nullptr};

  absl::optional<Api::IoCallUint64Result> checkReadResult() const;
  absl::optional<Api::IoCallUint64Result> checkWriteResult() const;
  Api::IoCallUint64Result copyOut(uint64_t max_length, Buffer::RawSlice* slices,
                                  uint64_t num_slice);
};

} // namespace Network
} // namespace Envoy
