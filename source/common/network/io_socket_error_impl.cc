#include "common/common/assert.h"
#include "common/network/io_socket_error_impl.h"

namespace Envoy {
namespace Network {

IoError::IoErrorCode IoSocketError::getErrorCode() const {
  switch (errno_) {
  case EAGAIN:
    RELEASE_ASSERT(this == getIoSocketEagainInstance(),
                   "Didn't use IoSocketEagain to represent EAGAIN.");
    return IoErrorCode::Again;
  case EBADF:
    return IoErrorCode::BadHandle;
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
