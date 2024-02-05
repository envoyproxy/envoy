#include "source/common/network/io_socket_error_impl.h"

#include "envoy/common/platform.h"

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"

namespace Envoy {
namespace Network {

Api::IoError::IoErrorCode IoSocketError::getErrorCode() const { return error_code_; }

std::string IoSocketError::getErrorDetails() const { return errorDetails(errno_); }

Api::IoErrorPtr IoSocketError::getIoSocketInvalidAddressError() {
  return Api::IoError::wrap(
      new IoSocketError(SOCKET_ERROR_NOT_SUP, Api::IoError::IoErrorCode::NoSupport));
}

Api::IoErrorPtr IoSocketError::create(int sys_errno) {
  return Api::IoError::wrap(new IoSocketError(sys_errno));
}

Api::IoErrorPtr IoSocketError::getIoSocketEbadfError() {
  return Api::IoError::wrap(new IoSocketError(SOCKET_ERROR_BADF, Api::IoError::IoErrorCode::BadFd));
}

Api::IoErrorPtr IoSocketError::getIoSocketEagainError() {
  static auto* instance = new IoSocketError(SOCKET_ERROR_AGAIN, Api::IoError::IoErrorCode::Again);
  return Api::IoError::reusedStatic(instance);
}

Api::IoCallUint64Result IoSocketError::ioResultSocketInvalidAddress() {
  return {0, getIoSocketInvalidAddressError()};
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
  case SOCKET_ERROR_NETUNREACH:
    return IoErrorCode::NetworkUnreachable;
  case SOCKET_ERROR_INVAL:
    return IoErrorCode::InvalidArgument;
  default:
    ENVOY_LOG_MISC(debug, "Unknown error code {} details {}", sys_errno, errorDetails(sys_errno));
    return IoErrorCode::UnknownError;
  }
}

} // namespace Network
} // namespace Envoy
