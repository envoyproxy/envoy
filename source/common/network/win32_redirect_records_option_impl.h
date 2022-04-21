#pragma once

#include "envoy/api/os_sys_calls.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/listen_socket.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Network {

class Win32RedirectRecordsOptionImpl : public Socket::Option,
                                       Logger::Loggable<Logger::Id::connection> {
public:
  Win32RedirectRecordsOptionImpl(const Win32RedirectRecords& redirect_records)
      : redirect_records_(redirect_records) {}

  // Socket::Option
  bool setOption(Socket& socket,
                 envoy::config::core::v3::SocketOption::SocketState state) const override;
  void hashKey(std::vector<uint8_t>&) const override;

  absl::optional<Details>
  getOptionDetails(const Socket& socket,
                   envoy::config::core::v3::SocketOption::SocketState) const override;
  bool isSupported() const override;

  static const Network::SocketOptionName& optionName();

private:
  static constexpr envoy::config::core::v3::SocketOption::SocketState in_state_ =
      envoy::config::core::v3::SocketOption::STATE_PREBIND;
  Win32RedirectRecords redirect_records_;
};

} // namespace Network
} // namespace Envoy
