#include "common/network/socket_option_impl.h"

#include "envoy/common/exception.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/network/address_impl.h"

namespace Envoy {
namespace Network {

// Socket::Option
bool SocketOptionImpl::setOption(Socket& socket, Socket::SocketState state) const {
  if (in_state_ == state) {
    const int error = SocketOptionImpl::setSocketOption(socket, optname_, value_);
    if (error != 0) {
      ENVOY_LOG(warn, "Setting option on socket failed: {}", strerror(errno));
      return false;
    }
  }
  return true;
}

bool SocketOptionImpl::isSupported() const { return optname_.has_value(); }

int SocketOptionImpl::setSocketOption(Socket& socket, Network::SocketOptionName optname,
                                      int value) {

  if (!optname.has_value()) {
    errno = ENOTSUP;
    return -1;
  }
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  return os_syscalls.setsockopt(socket.fd(), optname.value().first, optname.value().second, &value,
                                sizeof(value));
}

} // namespace Network
} // namespace Envoy
