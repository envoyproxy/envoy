#include "common/network/socket_option_impl.h"

#include "envoy/common/exception.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/network/address_impl.h"

namespace Envoy {
namespace Network {

// Socket::Option
bool SocketOptionImpl::setOption(Socket& socket,
                                 envoy::api::v2::core::SocketOption::SocketState state) const {
  if (in_state_ == state) {
    if (!optname_.has_value()) {
      ENVOY_LOG(warn, "Failed to set unsupported option on socket");
      return false;
    }

    const Api::SysCallIntResult result =
        SocketOptionImpl::setSocketOption(socket, optname_, value_);
    if (result.rc_ != 0) {
      ENVOY_LOG(warn, "Setting {} option on socket failed: {}", optname_.name(),
                strerror(result.errno_));
      return false;
    }
  }

  return true;
}

absl::optional<Socket::Option::Details>
SocketOptionImpl::getOptionDetails(const Socket&,
                                   envoy::api::v2::core::SocketOption::SocketState state) const {
  if (state != in_state_ || !isSupported()) {
    return absl::nullopt;
  }

  Socket::Option::Details info;
  info.name_ = optname_;
  info.value_ = value_;
  return absl::optional<Option::Details>(std::move(info));
}

bool SocketOptionImpl::isSupported() const { return optname_.has_value(); }

Api::SysCallIntResult SocketOptionImpl::setSocketOption(Socket& socket,
                                                        const Network::SocketOptionName& optname,
                                                        const absl::string_view value) {
  if (!optname.has_value()) {
    return {-1, ENOTSUP};
  }

  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  return os_syscalls.setsockopt(socket.ioHandle().fd(), optname.level(), optname.option(),
                                value.data(), value.size());
}

} // namespace Network
} // namespace Envoy
