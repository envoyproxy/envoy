#include "common/network/io_socket_error_impl.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Network {

Api::IoError::IoErrorCode IoSocketError::getErrorCode() const {
  switch (errno_) {
  case EAGAIN:
    RELEASE_ASSERT(this == getIoSocketEagainInstance(),
                   "Didn't use getIoSocketEagainInstance() to generate `Again`.");
    return IoErrorCode::Again;
  case ENOTSUP:
    return IoErrorCode::NoSupport;
  case EAFNOSUPPORT:
    return IoErrorCode::AddressFamilyNoSupport;
  case EINPROGRESS:
    return IoErrorCode::InProgress;
  case EPERM:
    return IoErrorCode::Permission;
  default:
    return IoErrorCode::UnknownError;
  }
}

std::string IoSocketError::getErrorDetails() const { return ::strerror(errno_); }

} // namespace Network
} // namespace Envoy
