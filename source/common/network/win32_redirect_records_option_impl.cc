#include "source/common/network/win32_redirect_records_option_impl.h"

#include "envoy/common/exception.h"
#include "envoy/config/core/v3/base.pb.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/scalar_to_byte_vector.h"
#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"

namespace Envoy {
namespace Network {

namespace {
// `IOCTL` controls unlike socket options do not have level parameter. So we arbitrarily define one
// in Envoy.
#define ENVOY_IOCTL_LEVEL 1

// We create an Envoy definition for `SIO_SET_WFP_CONNECTION_REDIRECT_RECORDS` to make the
// unit tests work on both platforms.
#ifdef SIO_SET_WFP_CONNECTION_REDIRECT_RECORDS
#define ENVOY_SIO_SET_WFP_CONNECTION_REDIRECT_RECORDS SIO_SET_WFP_CONNECTION_REDIRECT_RECORDS
#else
#define ENVOY_SIO_SET_WFP_CONNECTION_REDIRECT_RECORDS -1
#endif

Network::SocketOptionName getWfpRedirectionOption() {
  return ENVOY_MAKE_SOCKET_OPTION_NAME(ENVOY_IOCTL_LEVEL,
                                       ENVOY_SIO_SET_WFP_CONNECTION_REDIRECT_RECORDS);
}
} // namespace

const Network::SocketOptionName& Win32RedirectRecordsOptionImpl::optionName() {
  CONSTRUCT_ON_FIRST_USE(Network::SocketOptionName, getWfpRedirectionOption());
}

// Socket::Option
bool Win32RedirectRecordsOptionImpl::setOption(
    Socket& socket, envoy::config::core::v3::SocketOption::SocketState state) const {
  if (in_state_ == state) {
    unsigned long size = 0;
    const Api::SysCallIntResult result =
        socket.ioctl(ENVOY_SIO_SET_WFP_CONNECTION_REDIRECT_RECORDS,
                     const_cast<void*>(reinterpret_cast<const void*>(redirect_records_.buf_)),
                     redirect_records_.buf_size_, nullptr, 0, &size);
    if (result.return_value_ != 0) {
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
  return Socket::Option::Details{Win32RedirectRecordsOptionImpl::optionName(),
                                 std::string(in_buffer_bstr)};
}

bool Win32RedirectRecordsOptionImpl::isSupported() const {
  return Win32RedirectRecordsOptionImpl::optionName().hasValue() &&
         Win32RedirectRecordsOptionImpl::optionName().option() != -1;
}

} // namespace Network
} // namespace Envoy
