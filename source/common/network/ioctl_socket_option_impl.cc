#include "common/network/ioctl_socket_option_impl.h"

#include "envoy/common/exception.h"
#include "envoy/config/core/v3/base.pb.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/scalar_to_byte_vector.h"
#include "common/common/utility.h"
#include "common/network/address_impl.h"

namespace Envoy {
namespace Network {

// Socket::Option
bool IoctlSocketOptionImpl::setOption(
    Socket& socket, envoy::config::core::v3::SocketOption::SocketState state) const {
  if (in_state_ == state) {
    if (!optname_.hasValue()) {
      ENVOY_LOG(warn, "Failed to set unsupported control on socket");
      return false;
    }

    unsigned long size = 0;
    const Api::SysCallIntResult result = socket.genericIoctl(
        optname_.option(), inBuffer_, inBuffer_size_, outBuffer_, outBuffer_size_, size);
    if (result.rc_ != 0) {
      ENVOY_LOG(warn, "Setting {} control on socket failed: {}", optname_.name(),
                errorDetails(result.errno_));
      return false;
    }
  }

  return true;
}

 void IoctlSocketOptionImpl::hashKey(std::vector<uint8_t>& hash) const {
    std::string in_buffer_bstr(reinterpret_cast<const char*>(inBuffer_), inBuffer_size_);
    std::string out_buffer_bstr(reinterpret_cast<const char*>(outBuffer_), outBuffer_size_);
    pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(std::string(in_buffer_bstr + out_buffer_bstr)), hash);
 }

absl::optional<Socket::Option::Details>
IoctlSocketOptionImpl::getOptionDetails(const Socket&,
                                        envoy::config::core::v3::SocketOption::SocketState) const {

  std::string in_buffer_bstr(reinterpret_cast<const char*>(inBuffer_), inBuffer_size_);
  std::string out_buffer_bstr(reinterpret_cast<const char*>(outBuffer_), outBuffer_size_);

  return Socket::Option::Details{optname_, std::string(in_buffer_bstr + out_buffer_bstr)};
}

bool IoctlSocketOptionImpl::isSupported() const { return optname_.hasValue(); }

} // namespace Network
} // namespace Envoy
