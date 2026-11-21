#pragma once

#include <chrono>

#include "envoy/network/io_handle.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/io_socket_error_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Http11Connect {

// An IoHandle that is handed to the transport socket running *inside* a CONNECT tunnel (the
// "origin" socket). Reads and writes are served from in-memory buffers that the tunnel socket
// moves through the transport socket connected to the proxy. All non-I/O operations are delegated
// to the real IoHandle of the connection.
class TunnelIoHandle : public Network::IoHandle {
public:
  explicit TunnelIoHandle(Network::IoHandle& io_handle) : io_handle_(io_handle) {}

  // Bytes received from the proxy, waiting to be read by the origin socket.
  Buffer::Instance& inbound() { return inbound_; }
  // Bytes written by the origin socket, waiting to be sent to the proxy.
  Buffer::Instance& outbound() { return outbound_; }
  // Marks that the proxy connection has reached end of stream. Once the inbound buffer is drained,
  // reads will report EOF.
  void setReadEndStream() { read_end_stream_ = true; }
  // True if the origin socket has requested a write shutdown.
  bool writeShutdown() const { return write_shutdown_; }

  // Network::IoHandle
  os_fd_t fdDoNotUse() const override { return io_handle_.fdDoNotUse(); }
  void setAbortiveClose() override { io_handle_.setAbortiveClose(); }
  Api::IoCallUint64Result close() override {
    closed_ = true;
    return Api::ioCallUint64ResultNoError();
  }
  bool isOpen() const override { return !closed_ && io_handle_.isOpen(); }
  bool wasConnected() const override { return io_handle_.wasConnected(); }
  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;
  Api::IoCallUint64Result read(Buffer::Instance& buffer,
                               std::optional<uint64_t> max_length) override;
  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;
  Api::IoCallUint64Result write(Buffer::Instance& buffer) override;
  Api::IoCallUint64Result send(const void* buffer, size_t length) override;
  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice*, uint64_t, int,
                                  const Network::Address::Ip*,
                                  const Network::Address::Instance&) override {
    return notSupported();
  }
  Api::IoCallUint64Result recvmsg(Buffer::RawSlice*, const uint64_t, uint32_t,
                                  const UdpSaveCmsgConfig&, RecvMsgOutput&) override {
    return notSupported();
  }
  Api::IoCallUint64Result recvmmsg(RawSliceArrays&, uint32_t, const UdpSaveCmsgConfig&,
                                   RecvMsgOutput&) override {
    return notSupported();
  }
  Api::IoCallUint64Result recv(void* buffer, size_t length, int flags) override;
  bool supportsMmsg() const override { return false; }
  bool supportsUdpGro() const override { return false; }
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
  Api::SysCallIntResult ioctl(unsigned long control_code, void* in_buffer,
                              unsigned long in_buffer_len, void* out_buffer,
                              unsigned long out_buffer_len,
                              unsigned long* bytes_returned) override {
    return io_handle_.ioctl(control_code, in_buffer, in_buffer_len, out_buffer, out_buffer_len,
                            bytes_returned);
  }
  Api::SysCallIntResult setBlocking(bool blocking) override {
    return io_handle_.setBlocking(blocking);
  }
  std::optional<int> domain() override { return io_handle_.domain(); }
  absl::StatusOr<Network::Address::InstanceConstSharedPtr> localAddress() override {
    return io_handle_.localAddress();
  }
  absl::StatusOr<Network::Address::InstanceConstSharedPtr> peerAddress() override {
    return io_handle_.peerAddress();
  }
  Network::IoHandlePtr duplicate() override { return io_handle_.duplicate(); }
  // File events are owned by the connection and driven through the real IoHandle.
  void initializeFileEvent(Event::Dispatcher&, Event::FileReadyCb, Event::FileTriggerType,
                           uint32_t) override {}
  void activateFileEvents(uint32_t) override {}
  void enableFileEvents(uint32_t) override {}
  void resetFileEvents() override {}
  Api::SysCallIntResult shutdown(int how) override;
  std::optional<std::chrono::milliseconds> lastRoundTripTime() override {
    return io_handle_.lastRoundTripTime();
  }
  std::optional<uint64_t> congestionWindowInBytes() const override {
    return io_handle_.congestionWindowInBytes();
  }
  std::optional<std::string> interfaceName() override { return io_handle_.interfaceName(); }

private:
  static Api::IoCallUint64Result notSupported() {
    return {0, Network::IoSocketError::create(SOCKET_ERROR_NOT_SUP)};
  }
  // Returns the result to report when there is no inbound data available.
  Api::IoCallUint64Result emptyReadResult() const;
  Api::IoCallUint64Result closedResult() const {
    return {0, Network::IoSocketError::getIoSocketEbadfError()};
  }

  Network::IoHandle& io_handle_;
  Buffer::OwnedImpl inbound_;
  Buffer::OwnedImpl outbound_;
  bool read_end_stream_{false};
  bool write_shutdown_{false};
  bool closed_{false};
};

} // namespace Http11Connect
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
