#include "common/network/io_socket_error_impl.h"

#include "common/common/assert.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Network {

Api::IoError::IoErrorCode IoSocketError::getErrorCode() const {
  switch (errno_) {
  case SOCKET_ERROR_AGAIN:
    ASSERT(this == IoSocketError::getIoSocketEagainInstance(),
           "Didn't use getIoSocketEagainInstance() to generate `Again`.");
    return IoErrorCode::Again;
  case SOCKET_ERROR_NOT_SUP:
    return IoErrorCode::NoSupport;
  case SOCKET_ERROR_AF_NO_SUP:
    return IoErrorCode::AddressFamilyNoSupport;
  case SOCKET_ERROR_IN_PROGRESS:
    return IoErrorCode::InProgress;
  case SOCKET_ERROR_PERM:
    return IoErrorCode::Permission;
  case SOCKET_ERROR_MSG_SIZE:
    return IoErrorCode::MessageTooBig;
  case SOCKET_ERROR_INTR:
    return IoErrorCode::Interrupt;
  case SOCKET_ERROR_ADDR_NOT_AVAIL:
    return IoErrorCode::AddressNotAvailable;
  default:
    ENVOY_LOG_MISC(debug, "Unknown error code {} details {}", errno_, getErrorDetails());
    return IoErrorCode::UnknownError;
  }
}

std::string IoSocketError::getErrorDetails() const { return errorDetails(errno_); }

IoSocketError* IoSocketError::getIoSocketEagainInstance() {
  static auto* instance = new IoSocketError(SOCKET_ERROR_AGAIN);
  return instance;
}

void IoSocketError::deleteIoError(Api::IoError* err) {
  ASSERT(err != nullptr);
  if (err != getIoSocketEagainInstance()) {
    delete err;
  }
}

} // namespace Network
} // namespace Envoy
