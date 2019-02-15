#include "common/network/io_socket_handle_impl.h"

#include <errno.h>

#include <iostream>

using Envoy::Api::SysCallIntResult;
using Envoy::Api::SysCallSizeResult;

namespace Envoy {
namespace Network {

IoError::IoErrorCode IoSocketError::errorCode() const {
  switch (errno_) {
  case EAGAIN:
    RELEASE_ASSERT(false, "EAGAIN should use specific error ENVOY_ERROR_AGAIN");
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

std::string IoSocketError::errorDetails() const { return ::strerror(errno_); }

IoSocketHandleImpl::~IoSocketHandleImpl() {
  if (fd_ != -1) {
    IoSocketHandleImpl::close();
  }
}

// Deallocate memory only if the error is not ENVOY_ERROR_AGAIN.
void deleteIoError(IoError* err) {
  ASSERT(err != nullptr);
  if (err != ENVOY_ERROR_AGAIN) {
    delete err;
  }
}

IoHandleCallUintResult IoSocketHandleImpl::close() {
  ASSERT(fd_ != -1);
  const int rc = ::close(fd_);
  fd_ = -1;
  IoErrorPtr err(nullptr, deleteIoError);
  if (rc == -1) {
    // Sytem call failed.
    err = (errno == EAGAIN
               // EAGAIN is frequent enough that its memory allocation should be avoided.
               ? std::unique_ptr<IoError, IoErrorDeleterType>(ENVOY_ERROR_AGAIN, deleteIoError)
               : std::unique_ptr<IoError, IoErrorDeleterType>(new IoSocketError(errno),
                                                              deleteIoError));
    return IoHandleCallResult<uint64_t>(0, std::move(err));
  }
  return IoHandleCallResult<uint64_t>(rc, std::move(err));
}

bool IoSocketHandleImpl::isOpen() const { return fd_ != -1; }

} // namespace Network
} // namespace Envoy
