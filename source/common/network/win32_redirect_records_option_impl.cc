#include "common/network/win32_redirect_records_option_impl.h"

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
bool Win32RedirectRecordsOptionImpl::setOption(
    Socket& socket, envoy::config::core::v3::SocketOption::SocketState state) const {
  if (in_state_ == state) {
    unsigned long size = 0;
    const Api::SysCallIntResult result = socket.win32Ioctl(
        optname_.option(), const_cast<void*>(reinterpret_cast<const void*>(redirect_records_.buf_)),
        redirect_records_.buf_size_, nullptr, 0, &size);
    if (result.rc_ != 0) {
      ENVOY_LOG(warn, "Setting WFP records on socket failed: {}", errorDetails(result.errno_));
      return false;
    }
  }

  return true;
}

void Win32RedirectRecordsOptionImpl::hashKey(std::vector<uint8_t>& hash) const {
  absl::string_view in_buffer_bstr(reinterpret_cast<const char*>(redirect_records_.buf_),
                                   redirect_records_.buf_size_);
  pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(in_buffer_bstr), hash);
}

absl::optional<Socket::Option::Details> Win32RedirectRecordsOptionImpl::getOptionDetails(
    const Socket&, envoy::config::core::v3::SocketOption::SocketState) const {

  absl::string_view in_buffer_bstr(reinterpret_cast<const char*>(redirect_records_.buf_),
                                   redirect_records_.buf_size_);
  return Socket::Option::Details{optname_, std::string(in_buffer_bstr)};
}

bool Win32RedirectRecordsOptionImpl::isSupported() const { return optname_.hasValue(); }

} // namespace Network
} // namespace Envoy
