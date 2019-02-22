#include "common/network/io_socket_handle_impl.h"

#include <errno.h>

#include <iostream>

#include "envoy/buffer/buffer.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/stack_array.h"

using Envoy::Api::SysCallIntResult;
using Envoy::Api::SysCallSizeResult;

namespace Envoy {
namespace Network {

IoError::IoErrorCode IoSocketError::errorCode() const {
  std::cerr << "errno = " << errno_ << "\n";
  switch (errno_) {
  case EAGAIN:
    // EAGAIN should use specific error ENVOY_ERROR_AGAIN.
    NOT_REACHED_GCOVR_EXCL_LINE;
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
  return IoHandleCallResult<uint64_t>(rc, IoErrorPtr(nullptr, deleteIoError));
}

bool IoSocketHandleImpl::isOpen() const { return fd_ != -1; }

IoHandleCallUintResult IoSocketHandleImpl::readv(uint64_t max_length, Buffer::RawSlice* slices,
                                                 uint64_t num_slice) {
  STACK_ARRAY(iov, iovec, num_slice);
  uint64_t num_slices_to_read = 0;
  uint64_t num_bytes_to_read = 0;
  for (; num_slices_to_read < num_slice && num_bytes_to_read < max_length; num_slices_to_read++) {
    iov[num_slices_to_read].iov_base = slices[num_slices_to_read].mem_;
    const size_t slice_length = std::min(slices[num_slices_to_read].len_,
                                         static_cast<size_t>(max_length - num_bytes_to_read));
    iov[num_slices_to_read].iov_len = slice_length;
    num_bytes_to_read += slice_length;
  }
  ASSERT(num_bytes_to_read <= max_length);
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSizeResult result =
      os_syscalls.readv(fd_, iov.begin(), static_cast<int>(num_slices_to_read));
  return sysCallResultToIoHandleCallResult(result);
}

IoHandleCallUintResult IoSocketHandleImpl::writev(const Buffer::RawSlice* slices,
                                                  uint64_t num_slice) {
  STACK_ARRAY(iov, iovec, num_slice);
  uint64_t num_slices_to_write = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      iov[num_slices_to_write].iov_base = slices[i].mem_;
      iov[num_slices_to_write].iov_len = slices[i].len_;
      num_slices_to_write++;
    }
  }
  if (num_slices_to_write == 0) {
    return IoHandleCallUintResult(0, IoErrorPtr(nullptr, deleteIoError));
  }
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSizeResult result = os_syscalls.writev(fd_, iov.begin(), num_slices_to_write);
  return sysCallResultToIoHandleCallResult(result);
}

IoHandleCallUintResult
IoSocketHandleImpl::sysCallResultToIoHandleCallResult(const Api::SysCallSizeResult& result) {
  if (result.rc_ >= 0) {
    // Return nullptr as IoError upon success.
    return IoHandleCallUintResult(result.rc_, IoErrorPtr(nullptr, deleteIoError));
  }
  return IoHandleCallUintResult(
      /*rc=*/0, (result.errno_ == EAGAIN
                     // EAGAIN is frequent enough that its memory allocation should be avoided.
                     ? IoErrorPtr(ENVOY_ERROR_AGAIN, deleteIoError)
                     : IoErrorPtr(new IoSocketError(result.errno_), deleteIoError)));
}

} // namespace Network
} // namespace Envoy
