#include "common/network/socket_option_impl.h"

#include "envoy/common/exception.h"
#include "envoy/config/core/v3/base.pb.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/network/address_impl.h"

namespace Envoy {
namespace Network {

// Socket::Option
bool SocketOptionImpl::setOption(Socket& socket,
                                 envoy::config::core::v3::SocketOption::SocketState state) const {
  if (in_state_ == state) {
    if (!optname_.hasValue()) {
      ENVOY_LOG(warn, "Failed to set unsupported option on socket");
      return false;
    }

    const Api::SysCallIntResult result =
        SocketOptionImpl::setSocketOption(socket, optname_, value_.data(), value_.size());
    if (result.rc_ != 0) {
      ENVOY_LOG(warn, "Setting {} option on socket failed: {}", optname_.name(),
                errorDetails(result.errno_));
      return false;
    }
  }

  return true;
}

absl::optional<Socket::Option::Details>
SocketOptionImpl::getOptionDetails(const Socket&,
                                   envoy::config::core::v3::SocketOption::SocketState state) const {
  if (state != in_state_ || !isSupported()) {
    return absl::nullopt;
  }

  Socket::Option::Details info;
  info.name_ = optname_;
  info.value_ = {value_.begin(), value_.end()};
  return absl::make_optional(std::move(info));
}

bool SocketOptionImpl::isSupported() const { return optname_.hasValue(); }

Api::SysCallIntResult SocketOptionImpl::setSocketOption(Socket& socket,
                                                        const Network::SocketOptionName& optname,
                                                        const void* value, size_t size) {
  if (!optname.hasValue()) {
    return {-1, SOCKET_ERROR_NOT_SUP};
  }

  return socket.setSocketOption(optname.level(), optname.option(), value, size);
}

} // namespace Network
} // namespace Envoy
