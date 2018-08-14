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
    const Api::SysCallIntResult result =
        SocketOptionImpl::setSocketOption(socket, optname_, value_);
    if (result.rc_ != 0) {
      ENVOY_LOG(warn, "Setting option on socket failed: {}", strerror(result.errno_));
      return false;
    }
  }
  return true;
}

bool SocketOptionImpl::isSupported() const { return optname_.has_value(); }

Api::SysCallIntResult SocketOptionImpl::setSocketOption(Socket& socket,
                                                        Network::SocketOptionName optname,
                                                        const absl::string_view value) {

  if (!optname.has_value()) {
    return {-1, ENOTSUP};
  }
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  return os_syscalls.setsockopt(socket.fd(), optname.value().first, optname.value().second,
                                value.data(), value.size());
}

} // namespace Network
} // namespace Envoy
