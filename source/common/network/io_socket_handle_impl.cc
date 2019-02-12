#include "common/network/io_socket_handle_impl.h"

#include <errno.h>

#include <iostream>

#include "common/common/assert.h"

using Envoy::Api::SysCallIntResult;
using Envoy::Api::SysCallSizeResult;

namespace Envoy {
namespace Network {

IoError::IoErrorCode IoSocketError::getErrorCode() const {
  switch (errno) {
  case 0:
    return IoErrorCode::NoError;
  case EAGAIN:
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

std::string IoSocketError::getErrorDetails() const { return ::strerror(errno); }

IoSocketHandleImpl::~IoSocketHandleImpl() {
  if (fd_ != -1) {
    IoSocketHandleImpl::close();
  }
}

IoHandleCallIntResult IoSocketHandleImpl::close() {
  ASSERT(fd_ != -1);
  const int rc = ::close(fd_);
  fd_ = -1;
  return IoHandleCallResult<int>(rc, std::unique_ptr<IoSocketError>());
}

bool IoSocketHandleImpl::isOpen() const { return fd_ != -1; }

} // namespace Network
} // namespace Envoy
