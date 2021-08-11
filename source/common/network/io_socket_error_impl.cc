#include "source/common/network/io_socket_error_impl.h"

#include "envoy/common/platform.h"

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"

namespace Envoy {
namespace Network {

Api::IoError::IoErrorCode IoSocketError::getErrorCode() const { return error_code_; }

std::string IoSocketError::getErrorDetails() const { return errorDetails(errno_); }

IoSocketError* IoSocketError::getIoSocketInvalidAddressInstance() {
  static auto* instance =
      new IoSocketError(SOCKET_ERROR_NOT_SUP, Api::IoError::IoErrorCode::NoSupport);
  return instance;
}

IoSocketError* IoSocketError::getIoSocketEagainInstance() {
  static auto* instance = new IoSocketError(SOCKET_ERROR_AGAIN, Api::IoError::IoErrorCode::Again);
  return instance;
}

void IoSocketError::deleteIoError(Api::IoError* err) {
  ASSERT(err != nullptr);
  ASSERT(err != getIoSocketInvalidAddressInstance());
  if (err != getIoSocketEagainInstance()) {
    delete err;
  }
}

Api::IoCallUint64Result IoSocketError::ioResultSocketInvalidAddress() {
  return Api::IoCallUint64Result(
      0, Api::IoErrorPtr(getIoSocketInvalidAddressInstance(), [](IoError*) {}));
}

Api::IoError::IoErrorCode IoSocketError::errorCodeFromErrno(int sys_errno) {
  switch (sys_errno) {
  case SOCKET_ERROR_AGAIN:
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
  case SOCKET_ERROR_BADF:
    return IoErrorCode::BadFd;
  case SOCKET_ERROR_CONNRESET:
    return IoErrorCode::ConnectionReset;
  default:
    ENVOY_LOG_MISC(debug, "Unknown error code {} details {}", sys_errno, errorDetails(sys_errno));
    return IoErrorCode::UnknownError;
  }
}

} // namespace Network
} // namespace Envoy
