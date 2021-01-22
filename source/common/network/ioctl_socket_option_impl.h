#pragma once

#include "envoy/api/os_sys_calls.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/listen_socket.h"

#include "common/common/assert.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Network {

#define IOCTL_LEVEL 1

#ifdef SIO_SET_WFP_CONNECTION_REDIRECT_RECORDS
#define ENVOY_SOCKET_REDIRECT_RECORDS                                                              \
  ENVOY_MAKE_SOCKET_OPTION_NAME(IOCTL_LEVEL, SIO_SET_WFP_CONNECTION_REDIRECT_RECORDS)
#else
#define ENVOY_SOCKET_REDIRECT_RECORDS Network::SocketOptionName()
#endif

class IoctlSocketOptionImpl : public Socket::Option, Logger::Loggable<Logger::Id::connection> {
public:
  IoctlSocketOptionImpl(envoy::config::core::v3::SocketOption::SocketState in_state,
                        Network::SocketOptionName optname, void* inBuffer,
                        unsigned long inBufferSize, void* outBuffer, unsigned long outBufferSize)
      : in_state_(in_state), optname_(optname), inBuffer_(inBuffer), inBuffer_size_(inBufferSize),
        outBuffer_(outBuffer), outBuffer_size_(outBufferSize) {}

  // Socket::Option
  bool setOption(Socket& socket,
                 envoy::config::core::v3::SocketOption::SocketState state) const override;

  // The common socket options don't require a hash key.
  void hashKey(std::vector<uint8_t>&) const override {}

  absl::optional<Details>
  getOptionDetails(const Socket& socket,
                   envoy::config::core::v3::SocketOption::SocketState) const override;

  bool isSupported() const;

private:
  const envoy::config::core::v3::SocketOption::SocketState in_state_;
  const Network::SocketOptionName optname_;

  void* inBuffer_;
  unsigned long inBuffer_size_;
  void* outBuffer_;
  unsigned long outBuffer_size_;
};

} // namespace Network
} // namespace Envoy
