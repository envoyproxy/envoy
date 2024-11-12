#pragma once

#include <list>

#include "envoy/api/io_error.h"
#include "envoy/common/exception.h"
#include "envoy/network/io_handle.h"

#include "source/common/common/logger.h"
#include "source/common/network/io_socket_error_impl.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

using namespace Envoy::Network;

#define VCL_INVALID_SH uint32_t(~0)
#define VCL_SH_VALID(_sh) (_sh != static_cast<uint32_t>(~0))
#define VCL_SET_SH_INVALID(_sh) (_sh = static_cast<uint32_t>(~0))

class VclIoHandle : public Envoy::Network::IoHandle,
                    Logger::Loggable<Logger::Id::connection>,
                    NonCopyable {
public:
  VclIoHandle(uint32_t sh, os_fd_t fd) : sh_(sh), fd_(fd) {}
  ~VclIoHandle() override;

  // Network::IoHandle
  os_fd_t fdDoNotUse() const override { return fd_; }
  Api::IoCallUint64Result close() override;
  bool isOpen() const override;
  bool wasConnected() const override;
  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;
  Api::IoCallUint64Result read(Buffer::Instance& buffer,
                               absl::optional<uint64_t> max_length) override;
  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;
  Api::IoCallUint64Result write(Buffer::Instance& buffer) override;
  Api::IoCallUint64Result recv(void* buffer, size_t length, int flags) override;
  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice, int flags,
                                  const Envoy::Network::Address::Ip* self_ip,
                                  const Envoy::Network::Address::Instance& peer_address) override;
  Api::IoCallUint64Result recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                  uint32_t self_port, const UdpSaveCmsgConfig& save_cmsg_config,
                                  RecvMsgOutput& output) override;
  Api::IoCallUint64Result recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                   const UdpSaveCmsgConfig& save_cmsg_config,
                                   RecvMsgOutput& output) override;
  absl::optional<std::chrono::milliseconds> lastRoundTripTime() override;
  absl::optional<uint64_t> congestionWindowInBytes() const override;

  bool supportsMmsg() const override;
  bool supportsUdpGro() const override { return false; }

  Api::SysCallIntResult bind(Envoy::Network::Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult listen(int backlog) override;
  Envoy::Network::IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;
  Api::SysCallIntResult connect(Envoy::Network::Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult setOption(int level, int optname, const void* optval,
                                  socklen_t optlen) override;
  Api::SysCallIntResult getOption(int level, int optname, void* optval, socklen_t* optlen) override;
  Api::SysCallIntResult ioctl(unsigned long control_code, void* in_buffer,
                              unsigned long in_buffer_len, void* out_buffer,
                              unsigned long out_buffer_len, unsigned long* bytes_returned) override;
  Api::SysCallIntResult setBlocking(bool blocking) override;
  absl::optional<int> domain() override;
  Envoy::Network::Address::InstanceConstSharedPtr localAddress() override;
  Envoy::Network::Address::InstanceConstSharedPtr peerAddress() override;
  Api::SysCallIntResult shutdown(int) override { return {0, 0}; }

  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override;
  void activateFileEvents(uint32_t events) override { file_event_->activate(events); }
  void enableFileEvents(uint32_t events) override { file_event_->setEnabled(events); }
  void resetFileEvents() override;
  IoHandlePtr duplicate() override;

  absl::optional<std::string> interfaceName() override { return absl::nullopt; }

  void cb(uint32_t events) { THROW_IF_NOT_OK(cb_(events)); }
  void setCb(Event::FileReadyCb cb) { cb_ = cb; }
  void updateEvents(uint32_t events);
  uint32_t sh() const { return sh_; }
  void clearChildWrkListener() { wrk_listener_ = nullptr; }
  VclIoHandle* getParentListener() { return parent_listener_; }
  bool isWrkListener() { return parent_listener_ != nullptr; }

private:
  void setParentListener(VclIoHandle* parent_listener) { parent_listener_ = parent_listener; }

  uint32_t sh_{VCL_INVALID_SH};
  os_fd_t fd_;
  Event::FileEventPtr file_event_{nullptr};
  bool is_listener_{false};
  bool not_listened_{false};
  // Listener allocated on main thread and shared with worker. VCL listen not called on it.
  VclIoHandle* parent_listener_{nullptr};
  // Listener allocated on worker and associated to main thread (parent) listener. VCL listen called
  // on it.
  std::unique_ptr<VclIoHandle> wrk_listener_{nullptr};
  Event::FileReadyCb cb_;
};

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy
