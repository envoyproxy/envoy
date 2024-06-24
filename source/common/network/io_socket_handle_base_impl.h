#pragma once

#include "envoy/network/io_handle.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Network {

/**
 * IoHandle derivative for sockets.
 */
class IoSocketHandleBaseImpl : public IoHandle, protected Logger::Loggable<Logger::Id::io> {
public:
  IoSocketHandleBaseImpl(os_fd_t fd = INVALID_SOCKET, bool socket_v6only = false,
                         absl::optional<int> domain = absl::nullopt);
  ~IoSocketHandleBaseImpl() override;

  // TODO(sbelair2)  To be removed when the fd is fully abstracted from clients.
  os_fd_t fdDoNotUse() const override { return fd_; }
  bool isOpen() const override;
  bool supportsMmsg() const override;
  bool supportsUdpGro() const override;
  Api::SysCallIntResult setOption(int level, int optname, const void* optval,
                                  socklen_t optlen) override;
  Api::SysCallIntResult getOption(int level, int optname, void* optval, socklen_t* optlen) override;
  Api::SysCallIntResult ioctl(unsigned long, void*, unsigned long, void*, unsigned long,
                              unsigned long*) override;
  Api::SysCallIntResult setBlocking(bool blocking) override;
  absl::optional<int> domain() override;
  Address::InstanceConstSharedPtr localAddress() override;
  Address::InstanceConstSharedPtr peerAddress() override;
  absl::optional<std::chrono::milliseconds> lastRoundTripTime() override;
  absl::optional<uint64_t> congestionWindowInBytes() const override;
  absl::optional<std::string> interfaceName() override;

protected:
  os_fd_t fd_;
  int socket_v6only_;
  const absl::optional<int> domain_;
};

} // namespace Network
} // namespace Envoy
