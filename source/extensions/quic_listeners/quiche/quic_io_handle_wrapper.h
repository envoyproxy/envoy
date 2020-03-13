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
  os_fd_t fd() const override { return io_handle_.fd(); }
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
      return Api::IoCallUint64Result(0, Api::IoErrorPtr(new Network::IoSocketError(EBADF),
                                                        Network::IoSocketError::deleteIoError));
    }
    return io_handle_.recvmsg(slices, num_slice, self_port, output);
  }

private:
  Network::IoHandle& io_handle_;
  bool closed_{false};
};

} // namespace Quic
} // namespace Envoy
