#include "common/network/io_socket_handle_impl.h"

#include <errno.h>

#include <iostream>

#include "common/common/assert.h"

using Envoy::Api::SysCallIntResult;
using Envoy::Api::SysCallSizeResult;

namespace Envoy {
namespace Network {

Api::IoError::IoErrorCode IoSocketError::errorCode() const {
  switch (errno_) {
  case EAGAIN:
    // EAGAIN should use specific error ENVOY_ERROR_AGAIN.
    NOT_REACHED_GCOVR_EXCL_LINE;
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

std::string IoSocketError::errorDetails() const { return ::strerror(errno_); }

IoSocketHandleImpl::~IoSocketHandleImpl() {
  if (fd_ != -1) {
    IoSocketHandleImpl::close();
  }
}

// Deallocate memory only if the error is not ENVOY_ERROR_AGAIN.
void deleteIoError(Api::IoError* err) {
  ASSERT(err != nullptr);
  if (err != ENVOY_ERROR_AGAIN) {
    delete err;
  }
}

Api::IoCallUintResult IoSocketHandleImpl::close() {
  ASSERT(fd_ != -1);
  const int rc = ::close(fd_);
  fd_ = -1;
  return Api::IoCallResult<uint64_t>(rc, Api::IoErrorPtr(nullptr, deleteIoError));
}

bool IoSocketHandleImpl::isOpen() const { return fd_ != -1; }

} // namespace Network
} // namespace Envoy
