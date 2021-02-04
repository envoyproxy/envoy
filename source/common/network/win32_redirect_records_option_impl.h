#pragma once

#include "envoy/api/os_sys_calls.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/listen_socket.h"

#include "common/common/assert.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Network {

// `IOCTL` controls unlike socket options do not have level parameter. So we arbitrarily define one
// in Envoy.
#define ENVOY_IOCTL_LEVEL 1

#ifdef SIO_SET_WFP_CONNECTION_REDIRECT_RECORDS
#define ENVOY_SOCKET_REDIRECT_RECORDS                                                              \
  ENVOY_MAKE_SOCKET_OPTION_NAME(ENVOY_IOCTL_LEVEL, SIO_SET_WFP_CONNECTION_REDIRECT_RECORDS)
#else
#define ENVOY_SOCKET_REDIRECT_RECORDS Network::SocketOptionName()
#endif

class Win32RedirectRecordsOptionImpl : public Socket::Option,
                                       Logger::Loggable<Logger::Id::connection> {
public:
  Win32RedirectRecordsOptionImpl(Network::SocketOptionName optname,
                                 const Win32RedirectRecords& redirect_records)
      : optname_(optname), redirect_records_(redirect_records) {}

  // Socket::Option
  bool setOption(Socket& socket,
                 envoy::config::core::v3::SocketOption::SocketState state) const override;

  // The common socket options don't require a hash key.
  void hashKey(std::vector<uint8_t>&) const override;

  absl::optional<Details>
  getOptionDetails(const Socket& socket,
                   envoy::config::core::v3::SocketOption::SocketState) const override;

  bool isSupported() const;

private:
  const static envoy::config::core::v3::SocketOption::SocketState in_state_ =
      envoy::config::core::v3::SocketOption::STATE_PREBIND;
  const Network::SocketOptionName optname_;
  Win32RedirectRecords redirect_records_;
};

} // namespace Network
} // namespace Envoy
