#include <iostream>

#include "envoy/network/io_handle.h"

#include "common/network/io_socket_error_impl.h"

namespace Envoy {
namespace Quic {

// A wrapper class around IoHandle object which doesn't close() upon destruction. It is used to
// create ConnectionSocket as the actual IoHandle instance should out live connection socket.
class QuicIoHandleWrapper : public Network::IoHandle {
public:
  QuicIoHandleWrapper(Network::IoHandle& io_handle) : io_handle_(io_handle) {}

  // Network::IoHandle
  os_fd_t fdDoNotUse() const override { return io_handle_.fdDoNotUse(); }
  Api::IoCallUint64Result close() override {
    closed_ = true;
    return Api::ioCallUint64ResultNoError();
  }
  bool isOpen() const override { return !closed_; }
  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override {
    if (closed_) {
      return Api::IoCallUint64Result(0, Api::IoErrorPtr(new Network::IoSocketError(EBADF),
                                                        Network::IoSocketError::deleteIoError));
    }
    return io_handle_.readv(max_length, slices, num_slice);
  }
  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override {
    if (closed_) {
      return Api::IoCallUint64Result(0, Api::IoErrorPtr(new Network::IoSocketError(EBADF),
                                                        Network::IoSocketError::deleteIoError));
    }
    return io_handle_.writev(slices, num_slice);
  }
  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice, int flags,
                                  const Envoy::Network::Address::Ip* self_ip,
                                  const Network::Address::Instance& peer_address) override {
    if (closed_) {
      return Api::IoCallUint64Result(0, Api::IoErrorPtr(new Network::IoSocketError(EBADF),
                                                        Network::IoSocketError::deleteIoError));
    }
    return io_handle_.sendmsg(slices, num_slice, flags, self_ip, peer_address);
  }
  Api::IoCallUint64Result recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                  uint32_t self_port, RecvMsgOutput& output) override {
    if (closed_) {
      ASSERT(false, "recvmmsg is called after close.");
      return Api::IoCallUint64Result(0, Api::IoErrorPtr(new Network::IoSocketError(EBADF),
                                                        Network::IoSocketError::deleteIoError));
    }
    return io_handle_.recvmsg(slices, num_slice, self_port, output);
  }
  Api::IoCallUint64Result recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                   RecvMsgOutput& output) override {
    if (closed_) {
      ASSERT(false, "recvmmsg is called after close.");
      return Api::IoCallUint64Result(0, Api::IoErrorPtr(new Network::IoSocketError(EBADF),
                                                        Network::IoSocketError::deleteIoError));
    }
    return io_handle_.recvmmsg(slices, self_port, output);
  }
  Api::IoCallUint64Result recv(void* buffer, size_t length, int flags) override {
    if (closed_) {
      ASSERT(false, "recv called after close.");
      return Api::IoCallUint64Result(0, Api::IoErrorPtr(new Network::IoSocketError(EBADF),
                                                        Network::IoSocketError::deleteIoError));
    }
    return io_handle_.recv(buffer, length, flags);
  }
  bool supportsMmsg() const override { return io_handle_.supportsMmsg(); }
  bool supportsUdpGro() const override { return io_handle_.supportsUdpGro(); }
  Api::SysCallIntResult bind(Network::Address::InstanceConstSharedPtr address) override {
    return io_handle_.bind(address);
  }
  Api::SysCallIntResult listen(int backlog) override { return io_handle_.listen(backlog); }
  Network::IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override {
    return io_handle_.accept(addr, addrlen);
  }
  Api::SysCallIntResult connect(Network::Address::InstanceConstSharedPtr address) override {
    return io_handle_.connect(address);
  }
  Api::SysCallIntResult setOption(int level, int optname, const void* optval,
                                  socklen_t optlen) override {
    return io_handle_.setOption(level, optname, optval, optlen);
  }
  Api::SysCallIntResult getOption(int level, int optname, void* optval,
                                  socklen_t* optlen) override {
    return io_handle_.getOption(level, optname, optval, optlen);
  }
  Api::SysCallIntResult setBlocking(bool blocking) override {
    return io_handle_.setBlocking(blocking);
  }
  absl::optional<int> domain() override { return io_handle_.domain(); }
  Network::Address::InstanceConstSharedPtr localAddress() override {
    return io_handle_.localAddress();
  }
  Network::Address::InstanceConstSharedPtr peerAddress() override {
    return io_handle_.peerAddress();
  }
  Event::FileEventPtr createFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                                      Event::FileTriggerType trigger, uint32_t events) override {
    return io_handle_.createFileEvent(dispatcher, cb, trigger, events);
  }
  Api::SysCallIntResult shutdown(int how) override { return io_handle_.shutdown(how); }

private:
  Network::IoHandle& io_handle_;
  bool closed_{false};
};

} // namespace Quic
} // namespace Envoy
