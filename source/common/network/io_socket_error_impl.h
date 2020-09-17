#pragma once

#include "envoy/api/io_error.h"
#include "envoy/api/os_sys_calls_common.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Network {

class IoSocketError : public Api::IoError {
public:
  explicit IoSocketError(int sys_errno) : errno_(sys_errno) {}

  ~IoSocketError() override = default;

  Api::IoError::IoErrorCode getErrorCode() const override;
  std::string getErrorDetails() const override;

  // IoErrorCode::Again is used frequently. Define it to be a singleton to avoid frequent memory
  // allocation of such instance. If this is used, IoHandleCallResult has to be instantiated with
  // deleter deleteIoError() below to avoid deallocating memory for this error.
  static IoSocketError* getIoSocketEagainInstance();

  // This error is introduced when Envoy create socket for unsupported address. It is either a bug,
  // or this Envoy instance received config which is not yet supported. This should not be fatal
  // error.
  static Api::IoCallUint64Result ioResultSocketInvalidAddress();

  // Deallocate memory only if the error is not Again.
  static void deleteIoError(Api::IoError* err);

private:
  int errno_;
};

// Converts a SysCallSizeResult to IoCallUint64Result.
template <typename T>
Api::IoCallUint64Result sysCallResultToIoCallResult(const Api::SysCallResult<T>& result) {
  if (result.rc_ >= 0) {
    // Return nullptr as IoError upon success.
    return Api::IoCallUint64Result(result.rc_,
                                   Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError));
  }
  RELEASE_ASSERT(result.errno_ != SOCKET_ERROR_INVAL, "Invalid argument passed in.");
  return Api::IoCallUint64Result(
      /*rc=*/0,
      (result.errno_ == SOCKET_ERROR_AGAIN
           // EAGAIN is frequent enough that its memory allocation should be avoided.
           ? Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                             IoSocketError::deleteIoError)
           : Api::IoErrorPtr(new IoSocketError(result.errno_), IoSocketError::deleteIoError)));
}

} // namespace Network
} // namespace Envoy
